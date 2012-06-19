﻿#pragma once

#include "public.h"
#include "schema.h"
#include "key.h"
#include "chunk_writer_base.h"

#include <ytlib/table_client/table_chunk_meta.pb.h>
#include <ytlib/chunk_holder/chunk.pb.h>

#include <ytlib/chunk_client/public.h>
#include <ytlib/chunk_server/chunk_ypath_proxy.h>
#include <ytlib/misc/codec.h>
#include <ytlib/misc/thread_affinity.h>
#include <ytlib/misc/ref_counted.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

class  TPartitionChunkWriter
    : public TChunkWriterBase
{
public:
    TPartitionChunkWriter(
        TChunkWriterConfigPtr config,
        NChunkClient::IAsyncWriterPtr chunkWriter,
        const std::vector<TChannel>& channels,
        const TKeyColumns& keyColumns,
        const std::vector<NProto::TKey>& partitionKeys);

    ~TPartitionChunkWriter();

    bool TryWriteRow(const TRow& row);
    TAsyncError AsyncClose();

    i64 GetCurrentSize() const;
    NChunkHolder::NProto::TChunkMeta GetMasterMeta() const;

    i64 GetMetaSize() const;

private:
    TChannel Channel;

    std::vector<TOwningKey> PartitionKeys;
    yhash_map<TStringBuf, int> ColumnIndexes;
    int KeyColumnCount;

    //! Current size of written data.
    /*!
     *  - This counter is updated every #AsyncEndRow call.
     *  - This is an upper bound approximation of the size of written data.
     *    (Indeed, the counter includes compressed size of complete blocks and
     *    uncompressed size of incomplete blocks.)
     */
    i64 CurrentSize;

    std::vector<TChannelWriterPtr> ChannelWriters;

    i64 BasicMetaSize;

    
    NProto::TPartitionsExt PartitionsExt;

    void PrepareBlock(int partitionTag);

    void OnFinalBlocksWritten();

    DECLARE_THREAD_AFFINITY_SLOT(ClientThread);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
