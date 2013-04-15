﻿#include "stdafx.h"
#include "merge_job.h"
#include "private.h"
#include "job_detail.h"
#include "config.h"

#include <ytlib/meta_state/master_channel.h>

#include <ytlib/table_client/table_chunk_reader.h>
#include <ytlib/table_client/table_chunk_writer.h>
#include <ytlib/table_client/sync_reader.h>
#include <ytlib/table_client/sync_writer.h>
#include <ytlib/table_client/multi_chunk_sequential_reader.h>
#include <ytlib/table_client/multi_chunk_parallel_reader.h>

#include <ytlib/chunk_client/replication_reader.h>
#include <ytlib/chunk_client/multi_chunk_sequential_writer.h>
#include <ytlib/chunk_client/client_block_cache.h>

#include <ytlib/ytree/yson_string.h>

#include <ytlib/yson/lexer.h>

#include <server/chunk_server/public.h>

namespace NYT {
namespace NJobProxy {

using namespace NYTree;
using namespace NTableClient;
using namespace NChunkClient;
using namespace NChunkServer;
using namespace NScheduler::NProto;
using namespace NChunkClient::NProto;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& SILENT_UNUSED Logger = JobProxyLogger;
static NProfiling::TProfiler& SILENT_UNUSED Profiler = JobProxyProfiler;

////////////////////////////////////////////////////////////////////////////////

template <template <typename> class TMultiChunkReader>
class TMergeJob
    : public TJob
{
public:
    typedef TMultiChunkReader<TTableChunkReader> TReader;
    typedef TMultiChunkSequentialWriter<TTableChunkWriter> TWriter;

    explicit TMergeJob(IJobHost* host)
        : TJob(host)
        , JobSpec(Host->GetJobSpec())
        , SchedulerJobSpecExt(JobSpec.GetExtension(TSchedulerJobSpecExt::scheduler_job_spec_ext))
    {
        auto config = Host->GetConfig();

        YCHECK(SchedulerJobSpecExt.output_specs_size() == 1);

        std::vector<TInputChunk> inputChunks;
        FOREACH (const auto& inputSpec, SchedulerJobSpecExt.input_specs()) {
            FOREACH (const auto& inputChunk, inputSpec.chunks()) {
                inputChunks.push_back(inputChunk);
            }
        }

        auto readerProvider = New<TTableChunkReaderProvider>(config->JobIO->TableReader);
        Reader = CreateSyncReader(New<TReader>(
            config->JobIO->TableReader,
            Host->GetMasterChannel(),
            Host->GetBlockCache(),
            Host->GetNodeDirectory(),
            std::move(inputChunks),
            readerProvider));

        if (JobSpec.HasExtension(TMergeJobSpecExt::merge_job_spec_ext)) {
            const auto& mergeJobSpec = JobSpec.GetExtension(TMergeJobSpecExt::merge_job_spec_ext);
            KeyColumns = FromProto<Stroka>(mergeJobSpec.key_columns());
            LOG_INFO("Ordered merge produces sorted output");
        }

        // ToDo(psushin): estimate row count for writer.
        auto transactionId = FromProto<TTransactionId>(SchedulerJobSpecExt.output_transaction_id());
        const auto& outputSpec = SchedulerJobSpecExt.output_specs(0);
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

    virtual NScheduler::NProto::TJobResult Run() override
    {
        PROFILE_TIMING ("/merge_time") {
            LOG_INFO("Initializing");

            yhash_map<TStringBuf, int> keyColumnToIndex;

            {
                if (KeyColumns) {
                    for (int i = 0; i < KeyColumns->size(); ++i) {
                        TStringBuf name(~KeyColumns->at(i), KeyColumns->at(i).size());
                        keyColumnToIndex[name] = i;
                    }
                }

                Reader->Open();
                Writer->Open();
            }
            PROFILE_TIMING_CHECKPOINT("init");

            LOG_INFO("Merging");
            {
                NYson::TLexer lexer;
                // Unsorted write - use dummy key.
                TNonOwningKey key;
                if (KeyColumns) {
                    key.ClearAndResize(KeyColumns->size());
                }

                while (const TRow* row = Reader->GetRow()) {
                    if (KeyColumns) {
                        key.Clear();

                        FOREACH (const auto& pair, *row) {
                            auto it = keyColumnToIndex.find(pair.first);
                            if (it != keyColumnToIndex.end()) {
                                key.SetKeyPart(it->second, pair.second, lexer);
                            }
                        }
                        Writer->WriteRowUnsafe(*row, key);
                    } else {
                        Writer->WriteRowUnsafe(*row);
                    }
                }
            }
            PROFILE_TIMING_CHECKPOINT("merge");

            LOG_INFO("Finalizing");
            {
                Writer->Close();

                TJobResult result;
                ToProto(result.mutable_error(), TError());
                return result;
            }
        }
    }

    virtual double GetProgress() const override
    {
        i64 total = Reader->GetRowCount();
        if (total == 0) {
            LOG_WARNING("GetProgress: empty total");
            return 0;
        } else {
            double progress = (double) Reader->GetRowIndex() / total;
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

    ISyncReaderPtr Reader;
    ISyncWriterUnsafePtr Writer;

    TNullable<TKeyColumns> KeyColumns;

};

TJobPtr CreateOrderedMergeJob(IJobHost* host)
{
    return New< TMergeJob<TMultiChunkSequentialReader> >(host);
}

TJobPtr CreateUnorderedMergeJob(IJobHost* host)
{
    return New< TMergeJob<TMultiChunkParallelReader> >(host);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
