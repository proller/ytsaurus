﻿#include "stdafx.h"
#include "private.h"
#include "job_detail.h"
#include "config.h"
#include "simple_sort_job.h"
#include "small_key.h"

#include <ytlib/misc/sync.h>

#include <ytlib/meta_state/master_channel.h>

#include <ytlib/chunk_client/client_block_cache.h>
#include <ytlib/chunk_client/multi_chunk_sequential_writer.h>

#include <ytlib/table_client/table_chunk_writer.h>
#include <ytlib/table_client/table_chunk_reader.h>
#include <ytlib/table_client/multi_chunk_parallel_reader.h>
#include <ytlib/table_client/sync_writer.h>

#include <ytlib/yson/lexer.h>

namespace NYT {
namespace NJobProxy {

using namespace NElection;
using namespace NTableClient;
using namespace NChunkClient;
using namespace NObjectClient;
using namespace NYTree;
using namespace NScheduler::NProto;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& SILENT_UNUSED Logger = JobProxyLogger;
static NProfiling::TProfiler& SILENT_UNUSED Profiler = JobProxyProfiler;

typedef TMultiChunkParallelReader<TTableChunkReader> TReader;
typedef TMultiChunkSequentialWriter<TTableChunkWriter> TWriter;

////////////////////////////////////////////////////////////////////////////////

// ToDo(psushin): get rid of value count.

class TSimpleSortJob
    : public TJob
{
public:
    explicit TSimpleSortJob(IJobHost* host)
        : TJob(host)
        , JobSpec(Host->GetJobSpec())
        , SchedulerJobSpecExt(JobSpec.GetExtension(TSchedulerJobSpecExt::scheduler_job_spec_ext))
        , SortJobSpecExt(JobSpec.GetExtension(TSortJobSpecExt::sort_job_spec_ext))
    {
        auto config = Host->GetConfig();

        YCHECK(SchedulerJobSpecExt.input_specs_size() == 1);
        const auto& inputSpec = SchedulerJobSpecExt.input_specs(0);

        YCHECK(SchedulerJobSpecExt.output_specs_size() == 1);
        const auto& outputSpec = SchedulerJobSpecExt.output_specs(0);

        KeyColumns = FromProto<Stroka>(SortJobSpecExt.key_columns());

        {
            auto options = New<TChunkReaderOptions>();
            options->KeepBlocks = true;

            std::vector<NChunkClient::NProto::TInputChunk> chunks(
                inputSpec.chunks().begin(),
                inputSpec.chunks().end());

            srand(time(NULL));
            std::random_shuffle(chunks.begin(), chunks.end());

            auto provider = New<TTableChunkReaderProvider>(config->JobIO->TableReader, options);

            Reader = New<TReader>(
                config->JobIO->TableReader,
                Host->GetMasterChannel(),
                Host->GetBlockCache(),
                Host->GetNodeDirectory(),
                std::move(chunks),
                provider);
        }

        {
            auto transactionId = FromProto<TTransactionId>(SchedulerJobSpecExt.output_transaction_id());
            auto chunkListId = FromProto<TChunkListId>(outputSpec.chunk_list_id());
            auto options = ConvertTo<TTableWriterOptionsPtr>(TYsonString(outputSpec.table_writer_options()));
            options->KeyColumns = KeyColumns;

            auto writerProvider = New<TTableChunkWriterProvider>(
                config->JobIO->TableWriter,
                options);

            Writer = CreateSyncWriter<TTableChunkWriter>(New<TWriter>(
                config->JobIO->TableWriter,
                options,
                writerProvider,
                Host->GetMasterChannel(),
                transactionId,
                chunkListId));
        }
    }

    virtual NScheduler::NProto::TJobResult Run() override
    {
        PROFILE_TIMING ("/sort_time") {
            size_t keyColumnCount = KeyColumns.size();

            yhash_map<TStringBuf, int> keyColumnToIndex;

            std::vector< std::pair<TStringBuf, TStringBuf> > valueBuffer;
            std::vector<TSmallKeyPart> keyBuffer;
            std::vector<ui32> valueIndexBuffer;
            std::vector<ui32> rowIndexBuffer;

            auto estimatedRowCount = SchedulerJobSpecExt.input_row_count();

            LOG_INFO("Initializing");
            {
                for (int i = 0; i < KeyColumns.size(); ++i) {
                    TStringBuf name(~KeyColumns[i], KeyColumns[i].size());
                    keyColumnToIndex[name] = i;
                }

                Sync(~Reader, &TReader::AsyncOpen);

                valueBuffer.reserve(1000000);
                keyBuffer.reserve(estimatedRowCount * keyColumnCount);
                valueIndexBuffer.reserve(estimatedRowCount + 1);
                rowIndexBuffer.reserve(estimatedRowCount);

                // Add fake row.
                valueIndexBuffer.push_back(0);
            }

            PROFILE_TIMING_CHECKPOINT("init");

            LOG_INFO("Reading");
            {
                NYson::TLexer lexer;
                while (Reader->IsValid()) {
                    rowIndexBuffer.push_back(rowIndexBuffer.size());
                    YASSERT(rowIndexBuffer.back() <= std::numeric_limits<ui32>::max());

                    keyBuffer.resize(keyBuffer.size() + keyColumnCount);

                    FOREACH (const auto& pair, Reader->CurrentReader()->GetRow()) {
                        auto it = keyColumnToIndex.find(pair.first);
                        if (it != keyColumnToIndex.end()) {
                            auto& keyPart = keyBuffer[rowIndexBuffer.back() * keyColumnCount + it->second];
                            SetSmallKeyPart(keyPart, pair.second, lexer);
                        }
                        valueBuffer.push_back(pair);
                    }

                    valueIndexBuffer.push_back(valueBuffer.size());

                    if (!Reader->FetchNextItem()) {
                        Sync(~Reader, &TReader::GetReadyEvent);
                    }
                }
            }
            PROFILE_TIMING_CHECKPOINT("read");

            LOG_INFO("Sorting");

            std::sort(
                rowIndexBuffer.begin(),
                rowIndexBuffer.end(),
                [&] (ui32 lhs, ui32 rhs) -> bool {
                    for (int i = 0; i < keyColumnCount; ++i) {
                        auto res = CompareSmallKeyParts(
                            keyBuffer[lhs * keyColumnCount + i],
                            keyBuffer[rhs * keyColumnCount + i]);

                        if (res < 0)
                            return true;
                        if (res > 0)
                            return false;
                    }

                    return false;
            }
            );

            PROFILE_TIMING_CHECKPOINT("sort");

            LOG_INFO("Writing");
            {
                Writer->Open();

                TRow row;
                TNonOwningKey key(keyColumnCount);
                for (size_t progressIndex = 0; progressIndex < rowIndexBuffer.size(); ++progressIndex) {
                    row.clear();
                    key.Clear();

                    auto rowIndex = rowIndexBuffer[progressIndex];
                    for (auto valueIndex = valueIndexBuffer[rowIndex];
                        valueIndex < valueIndexBuffer[rowIndex + 1];
                        ++valueIndex)
                    {
                        row.push_back(valueBuffer[valueIndex]);
                    }

                    for (int keyIndex = 0; keyIndex < keyColumnCount; ++keyIndex) {
                        auto& keyPart = keyBuffer[rowIndex * keyColumnCount + keyIndex];
                        SetKeyPart(&key, keyPart, keyIndex);
                    }

                    Writer->WriteRowUnsafe(row, key);

                    if (progressIndex % 1000 == 0) {
                        Writer->SetProgress(double(progressIndex) / rowIndexBuffer.size());
                    }
                }

                Writer->Close();
            }

            PROFILE_TIMING_CHECKPOINT("write");

            LOG_INFO("Finalizing");
            {
                TJobResult result;
                ToProto(result.mutable_error(), TError());
                Writer->GetNodeDirectory()->DumpTo(result.mutable_node_directory());
                ToProto(result.mutable_chunks(), Writer->GetWrittenChunks());
                return result;
            }
        }
    }

    virtual double GetProgress() const override
    {
        i64 total = SchedulerJobSpecExt.input_row_count();
        if (total == 0) {
            LOG_WARNING("GetProgress: empty total");
            return 0;
        } else {
            // Split progress evenly between reading and writing.
            double progress =
                0.5 * Reader->GetItemIndex() / total +
                0.5 * Writer->GetRowCount() / total;
            LOG_DEBUG("GetProgress: %lf", progress);
            return progress;
        }
    }

    virtual std::vector<NChunkClient::TChunkId> GetFailedChunks() const override
    {
        return Reader->GetFailedChunks();
    }

private:
    const TJobSpec& JobSpec;
    const TSchedulerJobSpecExt& SchedulerJobSpecExt;
    const TSortJobSpecExt& SortJobSpecExt;

    TKeyColumns KeyColumns;

    TIntrusivePtr<TReader> Reader;
    ISyncWriterUnsafePtr Writer;

};

TJobPtr CreateSimpleSortJob(IJobHost* host)
{
    return New<TSimpleSortJob>(host);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
