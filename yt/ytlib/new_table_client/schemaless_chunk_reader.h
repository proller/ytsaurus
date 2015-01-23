#pragma once

#include "public.h"

#include "schemaless_reader.h"

#include <ytlib/chunk_client/chunk_reader_base.h>
#include <ytlib/chunk_client/multi_chunk_reader.h>
#include <ytlib/chunk_client/read_limit.h>

#include <ytlib/node_tracker_client/public.h>

#include <ytlib/transaction_client/public.h>

#include <core/rpc/public.h>

namespace NYT {
namespace NVersionedTableClient {

////////////////////////////////////////////////////////////////////////////////

struct ISchemalessChunkReader
    : public virtual NChunkClient::IChunkReaderBase
    , public ISchemalessReader
{ };

DEFINE_REFCOUNTED_TYPE(ISchemalessChunkReader)

////////////////////////////////////////////////////////////////////////////////

ISchemalessChunkReaderPtr CreateSchemalessChunkReader(
    TChunkReaderConfigPtr config,
    NChunkClient::IChunkReaderPtr underlyingReader,
    TNameTablePtr nameTable,
    NChunkClient::IBlockCachePtr uncompressedBlockCache,
    const TKeyColumns& keyColumns,
    const NChunkClient::NProto::TChunkMeta& masterMeta,
    const NChunkClient::TReadLimit& lowerLimit,
    const NChunkClient::TReadLimit& upperLimit,
    const TColumnFilter& columnFilter,
    i64 tableRowIndex = 0,
    TNullable<int> partitionTag = Null);

////////////////////////////////////////////////////////////////////////////////

struct ISchemalessMultiChunkReader
    : public virtual NChunkClient::IMultiChunkReader
    , public ISchemalessReader
{
    //! Table index of the last read row group.
    virtual int GetTableIndex() const = 0;

    //! Index of the next, unread row.
    virtual i64 GetSessionRowIndex() const = 0;

    //! Approximate row count readable with this reader.
    //! May change over time and finally converges to actually read row count.
    virtual i64 GetSessionRowCount() const = 0;

};

DEFINE_REFCOUNTED_TYPE(ISchemalessMultiChunkReader)

////////////////////////////////////////////////////////////////////////////////

ISchemalessMultiChunkReaderPtr CreateSchemalessSequentialMultiChunkReader(
    NChunkClient::TMultiChunkReaderConfigPtr config,
    NChunkClient::TMultiChunkReaderOptionsPtr options,
    NRpc::IChannelPtr masterChannel,
    NChunkClient::IBlockCachePtr compressedBlockCache,
    NChunkClient::IBlockCachePtr uncompressedBlockCache,
    NNodeTrackerClient::TNodeDirectoryPtr nodeDirectory,
    const std::vector<NChunkClient::NProto::TChunkSpec>& chunkSpecs,
    TNameTablePtr nameTable,
    const TKeyColumns& keyColumns = TKeyColumns());

////////////////////////////////////////////////////////////////////////////////

ISchemalessMultiChunkReaderPtr CreateSchemalessParallelMultiChunkReader(
    NChunkClient::TMultiChunkReaderConfigPtr config,
    NChunkClient::TMultiChunkReaderOptionsPtr options,
    NRpc::IChannelPtr masterChannel,
    NChunkClient::IBlockCachePtr compressedBlockCache,
    NChunkClient::IBlockCachePtr uncompressedBlockCache,
    NNodeTrackerClient::TNodeDirectoryPtr nodeDirectory,
    const std::vector<NChunkClient::NProto::TChunkSpec>& chunkSpecs,
    TNameTablePtr nameTable,
    const TKeyColumns& keyColumns = TKeyColumns());

////////////////////////////////////////////////////////////////////////////////

struct ISchemalessTableReader
    : public ISchemalessReader
{
    virtual i64 GetTableRowIndex() const = 0;

    //! Approximate row count readable with this reader.
    //! May change over time and finally converges to actually read row count.
    virtual i64 GetSessionRowCount() const = 0;
};

DEFINE_REFCOUNTED_TYPE(ISchemalessTableReader)

////////////////////////////////////////////////////////////////////////////////

ISchemalessTableReaderPtr CreateSchemalessTableReader(
    TTableReaderConfigPtr config,
    NRpc::IChannelPtr masterChannel,
    NTransactionClient::TTransactionPtr transaction,
    NChunkClient::IBlockCachePtr compressedBlockCache,
    NChunkClient::IBlockCachePtr uncompressedBlockCache,
    const NYPath::TRichYPath& richPath,
    TNameTablePtr nameTable);

////////////////////////////////////////////////////////////////////////////////

} // namespace NVersionedTableClient
} // namespace NYT
