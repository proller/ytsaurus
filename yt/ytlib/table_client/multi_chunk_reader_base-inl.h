#ifndef MULTI_CHUNK_READER_BASE_INL_H_
#error "Direct inclusion of this file is not allowed, include multi_chunk_reader_base.h"
#endif
#undef MULTI_CHUNK_READER_BASE_INL_H_

#include "private.h"
#include "config.h"

#include <ytlib/chunk_client/block_cache.h>
#include <ytlib/chunk_client/replication_reader.h>
#include <ytlib/chunk_client/async_reader.h>
#include <ytlib/chunk_client/dispatcher.h>
#include <ytlib/chunk_client/input_chunk.h>
#include <ytlib/chunk_client/chunk_meta_extensions.h>

#include <ytlib/node_tracker_client/node_directory.h>

#include <ytlib/actions/parallel_awaiter.h>

#include <ytlib/rpc/channel.h>

#include <ytlib/misc/protobuf_helpers.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

template <class TChunkReader>
TMultiChunkReaderBase<TChunkReader>::TMultiChunkReaderBase(
    TTableReaderConfigPtr config,
    NRpc::IChannelPtr masterChannel,
    NChunkClient::IBlockCachePtr blockCache,
    NNodeTrackerClient::TNodeDirectoryPtr nodeDirectory,
    std::vector<NChunkClient::NProto::TInputChunk>&& inputChunks,
    const TProviderPtr& readerProvider)
    : ItemIndex_(0)
    , ItemCount_(0)
    , IsFetchingComplete_(false)
    , Config(config)
    , MasterChannel(masterChannel)
    , BlockCache(blockCache)
    , NodeDirectory(nodeDirectory)
    , InputChunks(inputChunks)
    , ReaderProvider(readerProvider)
    , LastPreparedReader(-1)
    , FetchingCompleteAwaiter(New<TParallelAwaiter>())
    , Logger(TableReaderLogger)
{
    std::vector<i64> chunkDataSizes;
    chunkDataSizes.reserve(InputChunks.size());

    FOREACH (const auto& inputChunk, InputChunks) {
        i64 dataSize, rowCount;
        NChunkClient::GetStatistics(inputChunk, &dataSize, &rowCount);
        chunkDataSizes.push_back(dataSize);
        ItemCount_ += rowCount;
    }

    if (ReaderProvider->KeepInMemory()) {
        PrefetchWindow = MaxPrefetchWindow;
    } else {
        std::sort(chunkDataSizes.begin(), chunkDataSizes.end(), std::greater<i64>());

        PrefetchWindow = 0;
        i64 bufferSize = 0;
        while (PrefetchWindow < chunkDataSizes.size()) {
            bufferSize += std::min(
                chunkDataSizes[PrefetchWindow],
                config->WindowSize) + ChunkReaderMemorySize;
            if (bufferSize > Config->MaxBufferSize) {
                break;
            } else {
                ++PrefetchWindow;
            }
        }

        PrefetchWindow = std::min(PrefetchWindow, MaxPrefetchWindow);
        PrefetchWindow = std::max(PrefetchWindow, 1);
    }
    LOG_DEBUG("Preparing reader (PrefetchWindow: %d)",
        PrefetchWindow);
}

template <class TChunkReader>
void TMultiChunkReaderBase<TChunkReader>::PrepareNextChunk()
{
    int chunkSlicesSize = static_cast<int>(InputChunks.size());

    int chunkIndex = -1;

    {
        TGuard<TSpinLock> guard(NextChunkLock);
        LastPreparedReader = std::min(LastPreparedReader + 1, chunkSlicesSize);
        if (LastPreparedReader == chunkSlicesSize) {
            return;
        }
        chunkIndex = LastPreparedReader;
    }

    TSession session;
    session.ChunkIndex = chunkIndex;
    const auto& inputChunk = InputChunks[chunkIndex];
    auto chunkId = FromProto<NChunkClient::TChunkId>(inputChunk.chunk_id());
    auto replicas = FromProto<NChunkClient::TChunkReplica, NChunkClient::TChunkReplicaList>(inputChunk.replicas());

    LOG_DEBUG("Opening chunk (ChunkIndex: %d, ChunkId: %s)",
        chunkIndex,
        ~ToString(chunkId));

    auto remoteReader = CreateReplicationReader(
        Config,
        BlockCache,
        MasterChannel,
        NodeDirectory,
        Null,
        chunkId,
        replicas);

    session.Reader = ReaderProvider->CreateNewReader(inputChunk, remoteReader);

    session.Reader->AsyncOpen()
        .Subscribe(BIND(
            &TMultiChunkReaderBase<TChunkReader>::OnReaderOpened,
            MakeWeak(this),
            session)
        .Via(NChunkClient::TDispatcher::Get()->GetReaderInvoker()));
}

template <class TChunkReader>
void TMultiChunkReaderBase<TChunkReader>::ProcessOpenedReader(const TSession& session)
{
    LOG_DEBUG("Chunk opened (ChunkIndex: %d)", session.ChunkIndex);
    auto miscExt = GetProtoExtension<NChunkClient::NProto::TMiscExt>(
        InputChunks[session.ChunkIndex].extensions());
    ItemCount_ += session.Reader->GetRowCount() - miscExt.row_count();
    FetchingCompleteAwaiter->Await(session.Reader->GetFetchingCompleteEvent());
    if (FetchingCompleteAwaiter->GetRequestCount() == InputChunks.size()) {
        auto this_ = MakeStrong(this);
        FetchingCompleteAwaiter->Complete(BIND([=]() {
            this_->IsFetchingComplete_ = true;
        }));
    }
}

template <class TChunkReader>
void TMultiChunkReaderBase<TChunkReader>::ProcessFinishedReader(const TSession& session)
{
    ItemCount_ += session.Reader->GetRowIndex() - session.Reader->GetRowCount();
}

template <class TChunkReader>
void TMultiChunkReaderBase<TChunkReader>::AddFailedChunk(const TSession& session)
{
    const auto& inputChunk = InputChunks[session.ChunkIndex];
    auto chunkId = FromProto<NChunkClient::TChunkId>(inputChunk.chunk_id());
    LOG_DEBUG("Failed chunk added (ChunkId: %s)", ~ToString(chunkId));
    TGuard<TSpinLock> guard(FailedChunksLock);
    FailedChunks.push_back(chunkId);
}

template <class TChunkReader>
std::vector<NChunkClient::TChunkId> TMultiChunkReaderBase<TChunkReader>::GetFailedChunks() const
{
    TGuard<TSpinLock> guard(FailedChunksLock);
    return FailedChunks;
}

template <class TChunkReader>
TAsyncError TMultiChunkReaderBase<TChunkReader>::GetReadyEvent()
{
    return State.GetOperationError();
}

template <class TChunkReader>
const TIntrusivePtr<TChunkReader>& TMultiChunkReaderBase<TChunkReader>::CurrentReader() const
{
    return CurrentSession.Reader;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
