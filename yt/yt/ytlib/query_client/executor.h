#pragma once

#include "public.h"
#include "callbacks.h"

#include <yt/yt/ytlib/api/native/public.h>

#include <yt/yt/ytlib/node_tracker_client/public.h>

namespace NYT::NQueryClient {

////////////////////////////////////////////////////////////////////////////////

struct TQueryExecutorRowBufferTag
{ };

std::vector<std::pair<TDataSource, TString>> InferRanges(
    NApi::NNative::IConnectionPtr connection,
    TConstQueryPtr query,
    const TDataSource& dataSource,
    const TQueryOptions& options,
    TRowBufferPtr rowBuffer,
    const NLogging::TLogger& Logger);

IExecutorPtr CreateQueryExecutor(
    NApi::NNative::IConnectionPtr connection,
    IInvokerPtr invoker,
    IColumnEvaluatorCachePtr columnEvaluatorCache,
    IEvaluatorPtr evaluator,
    NNodeTrackerClient::INodeChannelFactoryPtr nodeChannelFactory,
    TFunctionImplCachePtr functionImplCache);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueryClient
