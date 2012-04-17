﻿#pragma once
#include "common.h"
#include "schema.h"
#include "async_writer.h"
#include "chunk_writer.h"

#include <ytlib/misc/thread_affinity.h>
#include <ytlib/actions/parallel_awaiter.h>
#include <ytlib/chunk_server/public.h>
#include <ytlib/chunk_server/chunk_service_proxy.h>
#include <ytlib/chunk_client/remote_writer.h>
#include <ytlib/cypress/cypress_service_proxy.h>
#include <ytlib/transaction_client/transaction.h>
#include <ytlib/transaction_server/transaction_ypath_proxy.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

class TChunkSequenceWriter
    : public IAsyncBlockWriter
{
public:
    typedef TIntrusivePtr<TChunkSequenceWriter> TPtr;

    struct TConfig
        : public TConfigurable
    {
        typedef TIntrusivePtr<TConfig> TPtr;

        i64 DesiredChunkSize;

        int TotalReplicaCount;
        int UploadReplicaCount;

        TChunkWriter::TConfig::TPtr ChunkWriter;
        NChunkClient::TRemoteWriter::TConfig::TPtr RemoteWriter;

        TConfig()
        {
            Register("desired_chunk_size", DesiredChunkSize)
                .GreaterThan(0)
                .Default(1024 * 1024 * 1024);
            Register("total_replica_count", TotalReplicaCount)
                .GreaterThanOrEqual(1)
                .Default(3);
            Register("upload_replica_count", UploadReplicaCount)
                .GreaterThanOrEqual(1)
                .Default(2);
            Register("chunk_writer", ChunkWriter)
                .DefaultNew();
            Register("remote_writer", RemoteWriter)
                .DefaultNew();
        }

        virtual void DoValidate() const
        {
            if (TotalReplicaCount < UploadReplicaCount) {
                ythrow yexception() << "\"total_replica_count\" cannot be less than \"upload_replica_count\"";
            }
        }
    };

    TChunkSequenceWriter(
        TConfig::TPtr config,
        NRpc::IChannel::TPtr masterChannel,
        const NTransactionClient::TTransactionId& transactionId,
        const NChunkServer::TChunkListId& parentChunkList,
        i64 expectedRowCount = std::numeric_limits<i64>::max());

    ~TChunkSequenceWriter();

    TAsyncError AsyncOpen(
        const NProto::TTableChunkAttributes& attributes);

    TAsyncError AsyncEndRow(
        const TKey& key,
        const std::vector<TChannelWriter::TPtr>& channels);

    TAsyncError AsyncClose(
        const std::vector<TChannelWriter::TPtr>& channels);

private:
    void CreateNextChunk();
    void InitCurrentChunk(TChunkWriter::TPtr nextChunk);
    void OnChunkCreated(
        NTransactionServer::TTransactionYPathProxy::TRspCreateObject::TPtr rsp);

    void FinishCurrentChunk(
        const std::vector<TChannelWriter::TPtr>& channels);

    void OnChunkClosed(
        TChunkWriter::TPtr currentChunk,
        TAsyncErrorPromise finishResult,
        TError error);

    void OnChunkRegistered(
        NChunkClient::TChunkId chunkId,
        TAsyncErrorPromise finishResult,
        NCypress::TCypressServiceProxy::TRspExecuteBatch::TPtr batchRsp);

    void OnChunkFinished(
        NChunkClient::TChunkId chunkId,
        TError error);

    void OnRowEnded(
        const std::vector<TChannelWriter::TPtr>& channels,
        TError error);

    void OnClose();

    TConfig::TPtr Config;
    NRpc::IChannel::TPtr MasterChannel;

    const i64 ExpectedRowCount;
    i64 CurrentRowCount;

    //! Total compressed size of data in the completed chunks.
    i64 CompleteChunkSize;

    const NObjectServer::TTransactionId TransactionId;
    const NChunkServer::TChunkListId ParentChunkList;

    TAsyncStreamState State;

    //! Protects #CurrentChunk.
    TChunkWriter::TPtr CurrentChunk;
    TPromise<TChunkWriter::TPtr> NextChunk;

    TParallelAwaiter::TPtr CloseChunksAwaiter;
    NProto::TTableChunkAttributes Attributes;

    DECLARE_THREAD_AFFINITY_SLOT(ClientThread);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
