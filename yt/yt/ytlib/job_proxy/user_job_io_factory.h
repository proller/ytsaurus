#pragma once

#include "public.h"

#include <yt/ytlib/api/native/public.h>

#include <yt/ytlib/chunk_client/public.h>
#include <yt/ytlib/chunk_client/chunk_reader.h>

#include <yt/ytlib/node_tracker_client/public.h>

#include <yt/ytlib/table_client/public.h>

#include <yt/ytlib/transaction_client/public.h>

#include <yt/core/actions/public.h>

#include <yt/core/concurrency/throughput_throttler.h>

namespace NYT::NJobProxy {

////////////////////////////////////////////////////////////////////////////////

struct IUserJobIOFactory
    : public virtual TRefCounted
{
    virtual NTableClient::ISchemalessMultiChunkReaderPtr CreateReader(
        NApi::NNative::IClientPtr client,
        const NNodeTrackerClient::TNodeDescriptor& nodeDescriptor,
        TClosure onNetworkReleased,
        NTableClient::TNameTablePtr nameTable,
        const NTableClient::TColumnFilter& columnFilter) = 0;

    virtual NTableClient::ISchemalessMultiChunkWriterPtr CreateWriter(
        NApi::NNative::IClientPtr client,
        NTableClient::TTableWriterConfigPtr config,
        NTableClient::TTableWriterOptionsPtr options,
        NChunkClient::TChunkListId chunkListId,
        NTransactionClient::TTransactionId transactionId,
        NTableClient::TTableSchemaPtr tableSchema,
        const NTableClient::TChunkTimestamps& chunkTimestamps) = 0;
};

DEFINE_REFCOUNTED_TYPE(IUserJobIOFactory)

////////////////////////////////////////////////////////////////////////////////

IUserJobIOFactoryPtr CreateUserJobIOFactory(
    const IJobSpecHelperPtr& jobSpecHelper,
    const NChunkClient::TClientBlockReadOptions& blockReadOptions,
    NChunkClient::TTrafficMeterPtr trafficMeter,
    NConcurrency::IThroughputThrottlerPtr inBandwidthThrottler,
    NConcurrency::IThroughputThrottlerPtr outBandwidthThrottler,
    NConcurrency::IThroughputThrottlerPtr outRpsThrottler);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJobProxy
