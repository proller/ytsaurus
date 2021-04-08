#include "query_executor.h"
#include "private.h"
#include "config.h"

#include <yt/yt/server/node/cluster_node/bootstrap.h>
#include <yt/yt/server/node/cluster_node/config.h>

#include <yt/yt/server/node/data_node/chunk_block_manager.h>
#include <yt/yt/server/node/data_node/chunk.h>
#include <yt/yt/server/node/data_node/chunk_registry.h>
#include <yt/yt/server/node/data_node/local_chunk_reader.h>
#include <yt/yt/server/node/data_node/legacy_master_connector.h>

#include <yt/yt/server/lib/hydra/hydra_manager.h>

#include <yt/yt/server/lib/misc/profiling_helpers.h>

#include <yt/yt/server/lib/tablet_node/config.h>
#include <yt/yt/server/node/tablet_node/security_manager.h>
#include <yt/yt/server/node/tablet_node/tablet.h>
#include <yt/yt/server/node/tablet_node/tablet_manager.h>
#include <yt/yt/server/node/tablet_node/tablet_reader.h>
#include <yt/yt/server/node/tablet_node/tablet_slot.h>
#include <yt/yt/server/node/tablet_node/tablet_profiling.h>
#include <yt/yt/server/node/tablet_node/tablet_snapshot_store.h>

#include <yt/yt/ytlib/api/native/connection.h>
#include <yt/yt/ytlib/api/native/client.h>

#include <yt/yt/ytlib/chunk_client/block_cache.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader_statistics.h>
#include <yt/yt_proto/yt/client/chunk_client/proto/chunk_spec.pb.h>
#include <yt/yt/ytlib/chunk_client/helpers.h>
#include <yt/yt/ytlib/chunk_client/replication_reader.h>

#include <yt/yt/ytlib/node_tracker_client/public.h>
#include <yt/yt/client/node_tracker_client/node_directory.h>

#include <yt/yt/client/object_client/helpers.h>

#include <yt/yt/client/query_client/query_statistics.h>

#include <yt/yt/ytlib/query_client/column_evaluator.h>
#include <yt/yt/ytlib/query_client/coordinator.h>
#include <yt/yt/ytlib/query_client/evaluator.h>
#include <yt/yt/ytlib/query_client/functions_cache.h>
#include <yt/yt/ytlib/query_client/helpers.h>
#include <yt/yt/ytlib/query_client/query.h>
#include <yt/yt/ytlib/query_client/query_helpers.h>
#include <yt/yt/ytlib/query_client/private.h>
#include <yt/yt/ytlib/query_client/executor.h>
#include <yt/yt/ytlib/query_client/coordination_helpers.h>

#include <yt/yt/ytlib/security_client/permission_cache.h>

#include <yt/yt/ytlib/table_client/chunk_meta_extensions.h>
#include <yt/yt/ytlib/table_client/config.h>
#include <yt/yt/client/table_client/pipe.h>
#include <yt/yt/ytlib/table_client/schemaful_chunk_reader.h>
#include <yt/yt/client/table_client/unversioned_reader.h>
#include <yt/yt/client/table_client/unversioned_writer.h>
#include <yt/yt/client/table_client/unordered_schemaful_reader.h>

#include <yt/yt/ytlib/tablet_client/public.h>

#include <yt/yt/ytlib/misc/memory_usage_tracker.h>

#include <yt/yt/core/concurrency/scheduler.h>

#include <yt/yt/core/misc/string.h>
#include <yt/yt/core/misc/collection_helpers.h>
#include <yt/yt/core/misc/tls_cache.h>
#include <yt/yt/core/misc/chunked_memory_pool.h>

#include <yt/yt/core/rpc/authentication_identity.h>

namespace NYT::NQueryClient {

////////////////////////////////////////////////////////////////////////////////

template <>
TRow GetPivotKey(const NTabletNode::TPartitionSnapshotPtr& shard)
{
    return shard->PivotKey;
}

template <>
TRow GetNextPivotKey(const NTabletNode::TPartitionSnapshotPtr& shard)
{
    return shard->NextPivotKey;
}

template <>
TRange<TRow> GetSampleKeys(const NTabletNode::TPartitionSnapshotPtr& shard)
{
    return shard->SampleKeys->Keys;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueryClient

namespace NYT::NQueryAgent {

using namespace NClusterNode;
using namespace NChunkClient;
using namespace NConcurrency;
using namespace NDataNode;
using namespace NNodeTrackerClient;
using namespace NObjectClient;
using namespace NProfiling;
using namespace NQueryClient;
using namespace NTableClient;
using namespace NTabletClient;
using namespace NTabletNode;
using namespace NYTree;

using NTabletNode::TTabletSnapshotPtr;

////////////////////////////////////////////////////////////////////////////////

namespace {

TColumnFilter GetColumnFilter(const TTableSchema& desiredSchema, const TTableSchema& tabletSchema)
{
    // Infer column filter.
    TColumnFilter::TIndexes columnFilterIndexes;
    for (const auto& column : desiredSchema.Columns()) {
        const auto& tabletColumn = tabletSchema.GetColumnOrThrow(column.Name());
        if (tabletColumn.GetPhysicalType() != column.GetPhysicalType()) {
            THROW_ERROR_EXCEPTION("Mismatched type of column %Qv in schema: expected %Qlv, found %Qlv",
                column.Name(),
                tabletColumn.GetPhysicalType(),
                column.GetPhysicalType());
        }
        columnFilterIndexes.push_back(tabletSchema.GetColumnIndex(tabletColumn));
    }

    return TColumnFilter(std::move(columnFilterIndexes));
}

class TProfilingReaderWrapper
    : public ISchemafulUnversionedReader
{
private:
    const ISchemafulUnversionedReaderPtr Underlying_;
    const TSelectReadCounters Counters_;

public:
    TProfilingReaderWrapper(
        ISchemafulUnversionedReaderPtr underlying,
        TSelectReadCounters counters)
        : Underlying_(std::move(underlying))
        , Counters_(std::move(counters))
    { }

    virtual IUnversionedRowBatchPtr Read(const TRowBatchReadOptions& options) override
    {
        return Underlying_->Read(options);
    }

    virtual TFuture<void> GetReadyEvent() const override
    {
        return Underlying_->GetReadyEvent();
    }

    virtual NChunkClient::NProto::TDataStatistics GetDataStatistics() const override
    {
        return Underlying_->GetDataStatistics();
    }

    virtual NChunkClient::TCodecStatistics GetDecompressionStatistics() const override
    {
        return Underlying_->GetDecompressionStatistics();
    }

    virtual bool IsFetchingCompleted() const override
    {
        return false;
    }

    virtual std::vector<TChunkId> GetFailedChunkIds() const override
    {
        return {};
    }

    ~TProfilingReaderWrapper()
    {
        auto statistics = GetDataStatistics();
        auto decompressionCpuTime = GetDecompressionStatistics().GetTotalDuration();

        Counters_.RowCount.Increment(statistics.row_count());
        Counters_.DataWeight.Increment(statistics.data_weight());
        Counters_.UnmergedRowCount.Increment(statistics.unmerged_row_count());
        Counters_.UnmergedDataWeight.Increment(statistics.unmerged_data_weight());
        Counters_.DecompressionCpuTime.Add(decompressionCpuTime);
    }
};

} // namespace

////////////////////////////////////////////////////////////////////////////////

struct TQuerySubexecutorBufferTag
{ };

////////////////////////////////////////////////////////////////////////////////

class TTabletSnapshotCache
{
public:
    TTabletSnapshotCache(
        ITabletSnapshotStorePtr snapshotStore,
        const NLogging::TLogger& logger)
        : SnapshotStore_(std::move(snapshotStore))
        , Logger(std::move(logger))
    { }

    void ValidateAndRegisterTabletSnapshot(
        TTabletId tabletId,
        TCellId cellId,
        NHydra::TRevision mountRevision,
        TTimestamp timestamp,
        bool suppressAccessTracking)
    {
        auto tabletSnapshot = SnapshotStore_->GetTabletSnapshotOrThrow(tabletId, cellId, mountRevision);

        SnapshotStore_->ValidateTabletAccess(tabletSnapshot, timestamp);

        Map_.emplace(tabletId, tabletSnapshot);

        if (!MultipleTables_) {
            if (TableId_ && tabletSnapshot->TableId != TableId_) {
                YT_LOG_ERROR("Found different tables in query, profiling will be incorrect (TableId1: %v, TableId2: %v)",
                    TableId_,
                    tabletSnapshot->TableId);
                MultipleTables_ = true;
            }

            TableId_ = tabletSnapshot->TableId;
            TableProfiler_ = tabletSnapshot->TableProfiler;
        }

        if (!suppressAccessTracking) {
            tabletSnapshot->TabletRuntimeData->AccessTime = NProfiling::GetInstant();
        }
    }

    TTableProfilerPtr GetTableProfiler()
    {
        if (MultipleTables_ || !TableProfiler_) {
            return TTableProfiler::GetDisabled();
        }

        return TableProfiler_;
    }

    TTabletSnapshotPtr GetCachedTabletSnapshot(TTabletId tabletId)
    {
        return GetOrCrash(Map_, tabletId);
    }

private:
    const ITabletSnapshotStorePtr SnapshotStore_;
    const NLogging::TLogger Logger;

    THashMap<TTabletId, TTabletSnapshotPtr> Map_;
    TTableId TableId_;

    NProfiling::TTagIdList ProfilerTags_;
    bool MultipleTables_ = false;
    TTableProfilerPtr TableProfiler_;
};

////////////////////////////////////////////////////////////////////////////////

class TQueryExecution
    : public TRefCounted
{
public:
    TQueryExecution(
        TQueryAgentConfigPtr config,
        TFunctionImplCachePtr functionImplCache,
        TBootstrap* bootstrap,
        IEvaluatorPtr evaluator,
        TConstQueryPtr query,
        TConstExternalCGInfoPtr externalCGInfo,
        std::vector<TDataSource> dataSources,
        IUnversionedRowsetWriterPtr writer,
        IMemoryChunkProviderPtr memoryChunkProvider,
        IInvokerPtr invoker,
        const TClientChunkReadOptions& chunkReadOptions,
        const TQueryOptions& queryOptions)
        : Config_(std::move(config))
        , FunctionImplCache_(std::move(functionImplCache))
        , Bootstrap_(bootstrap)
        , ColumnEvaluatorCache_(Bootstrap_
            ->GetMasterClient()
            ->GetNativeConnection()
            ->GetColumnEvaluatorCache())
        , Evaluator_(std::move(evaluator))
        , Query_(std::move(query))
        , ExternalCGInfo_(std::move(externalCGInfo))
        , DataSources_(std::move(dataSources))
        , Writer_(std::move(writer))
        , MemoryChunkProvider_(std::move(memoryChunkProvider))
        , Invoker_(std::move(invoker))
        , QueryOptions_(std::move(queryOptions))
        , ChunkReadOptions_(chunkReadOptions)
        , Logger(MakeQueryLogger(Query_))
        , TabletSnapshots_(Bootstrap_->GetTabletSnapshotStore(), Logger)
        , Identity_(NRpc::GetCurrentAuthenticationIdentity())
    { }

    TQueryStatistics Execute(TServiceProfilerGuard& profilerGuard)
    {
        for (const auto& source : DataSources_) {
            auto type = TypeFromId(source.ObjectId);
            switch (type) {
                case  EObjectType::Tablet:
                    TabletSnapshots_.ValidateAndRegisterTabletSnapshot(
                        source.ObjectId,
                        source.CellId,
                        source.MountRevision,
                        QueryOptions_.Timestamp,
                        QueryOptions_.SuppressAccessTracking);
                    break;
                default:
                    THROW_ERROR_EXCEPTION("Unsupported data source type %Qlv",
                        type);
            }
        }

        auto counters = TabletSnapshots_.GetTableProfiler()->GetQueryServiceCounters(GetCurrentProfilingUser());
        profilerGuard.Start(counters->Execute);

        return DoExecute();
    }

private:
    const TQueryAgentConfigPtr Config_;
    const TFunctionImplCachePtr FunctionImplCache_;
    TBootstrap* const Bootstrap_;
    const IColumnEvaluatorCachePtr ColumnEvaluatorCache_;
    const IEvaluatorPtr Evaluator_;

    const TConstQueryPtr Query_;

    const TConstExternalCGInfoPtr ExternalCGInfo_;
    const std::vector<TDataSource> DataSources_;
    const IUnversionedRowsetWriterPtr Writer_;
    const IMemoryChunkProviderPtr MemoryChunkProvider_;

    const IInvokerPtr Invoker_;
    const TQueryOptions QueryOptions_;
    const TClientChunkReadOptions ChunkReadOptions_;

    const NLogging::TLogger Logger;

    TTabletSnapshotCache TabletSnapshots_;

    const NRpc::TAuthenticationIdentity Identity_;

    typedef std::function<ISchemafulUnversionedReaderPtr()> TSubreaderCreator;

    void LogSplits(const std::vector<TDataSource>& splits)
    {
        if (QueryOptions_.VerboseLogging) {
            for (const auto& split : splits) {
                YT_LOG_DEBUG("Ranges in split %v: %v",
                    split.ObjectId,
                    MakeFormattableView(split.Ranges, TRangeFormatter()));
            }
        }
    }

    TQueryStatistics DoCoordinateAndExecute(
        std::vector<TRefiner> refiners,
        std::vector<TSubreaderCreator> subreaderCreators,
        std::vector<std::vector<TDataSource>> readRanges)
    {
        auto clientOptions = NApi::TClientOptions::FromAuthenticationIdentity(Identity_);
        auto client = Bootstrap_
            ->GetMasterClient()
            ->GetNativeConnection()
            ->CreateNativeClient(clientOptions);

        auto remoteExecutor = CreateQueryExecutor(
            client->GetNativeConnection(),
            Invoker_,
            ColumnEvaluatorCache_,
            Evaluator_,
            client->GetChannelFactory(),
            FunctionImplCache_);

        auto functionGenerators = New<TFunctionProfilerMap>();
        auto aggregateGenerators = New<TAggregateProfilerMap>();
        MergeFrom(functionGenerators.Get(), *BuiltinFunctionProfilers);
        MergeFrom(aggregateGenerators.Get(), *BuiltinAggregateProfilers);
        FetchFunctionImplementationsFromCypress(
            functionGenerators,
            aggregateGenerators,
            ExternalCGInfo_,
            FunctionImplCache_,
            ChunkReadOptions_);

        return CoordinateAndExecute(
            Query_,
            Writer_,
            refiners,
            [&] (const TConstQueryPtr& subquery, int index) {
                auto asyncSubqueryResults = std::make_shared<std::vector<TFuture<TQueryStatistics>>>();

                auto foreignProfileCallback = [
                    asyncSubqueryResults,
                    remoteExecutor,
                    dataSplits = std::move(readRanges[index]),
                    this,
                    this_ = MakeStrong(this)
                ] (const TQueryPtr& subquery, const TConstJoinClausePtr& joinClause) -> TJoinSubqueryEvaluator {
                    auto remoteOptions = QueryOptions_;
                    remoteOptions.MaxSubqueries = 1;

                    size_t minKeyWidth = std::numeric_limits<size_t>::max();
                    for (const auto& split : dataSplits) {
                        minKeyWidth = std::min(minKeyWidth, split.KeyWidth);
                    }

                    YT_LOG_DEBUG("Profiling query (CommonKeyPrefix: %v, MinKeyWidth: %v)",
                        joinClause->CommonKeyPrefix,
                        minKeyWidth);

                    if (joinClause->CommonKeyPrefix >= minKeyWidth && minKeyWidth > 0) {
                        auto rowBuffer = New<TRowBuffer>(TQuerySubexecutorBufferTag(), MemoryChunkProvider_);

                        std::vector<TRowRange> prefixRanges;
                        std::vector<TRow> prefixKeys;
                        bool isRanges = false;
                        bool isKeys = false;

                        std::vector<EValueType> schema;
                        for (const auto& split : dataSplits) {
                            for (int index = 0; index < split.Ranges.Size(); ++index) {
                                isRanges = true;
                                YT_VERIFY(!isKeys);
                                const auto& range = split.Ranges[index];
                                int lowerBoundWidth = std::min(
                                    GetSignificantWidth(range.first),
                                    joinClause->CommonKeyPrefix);

                                auto lowerBound = rowBuffer->AllocateUnversioned(lowerBoundWidth);
                                for (int column = 0; column < lowerBoundWidth; ++column) {
                                    lowerBound[column] = rowBuffer->Capture(range.first[column]);
                                }

                                int upperBoundWidth = std::min(
                                    GetSignificantWidth(range.second),
                                    joinClause->CommonKeyPrefix);

                                auto upperBound = WidenKeySuccessor(
                                    range.second,
                                    upperBoundWidth,
                                    rowBuffer,
                                    true);

                                prefixRanges.emplace_back(lowerBound, upperBound);

                                YT_LOG_DEBUG_IF(QueryOptions_.VerboseLogging, "Transforming range [%v .. %v] -> [%v .. %v]",
                                    range.first,
                                    range.second,
                                    lowerBound,
                                    upperBound);
                            }

                            schema = split.Schema;

                            for (int index = 0; index < split.Keys.Size(); ++index) {
                                isKeys = true;
                                YT_VERIFY(!isRanges);
                                const auto& key = split.Keys[index];

                                int keyWidth = std::min(
                                    size_t(key.GetCount()),
                                    joinClause->CommonKeyPrefix);

                                auto prefixKey = rowBuffer->AllocateUnversioned(keyWidth);
                                for (int column = 0; column < keyWidth; ++column) {
                                    prefixKey[column] = rowBuffer->Capture(key[column]);
                                }
                                prefixKeys.emplace_back(prefixKey);
                            }
                        }

                        TDataSource dataSource;
                        dataSource.ObjectId = joinClause->ForeignObjectId;
                        dataSource.CellId = joinClause->ForeignCellId;

                        if (isRanges) {
                            prefixRanges.erase(
                                MergeOverlappingRanges(prefixRanges.begin(), prefixRanges.end()),
                                prefixRanges.end());
                            dataSource.Ranges = MakeSharedRange(prefixRanges, rowBuffer);
                        }

                        if (isKeys) {
                            prefixKeys.erase(std::unique(prefixKeys.begin(), prefixKeys.end()), prefixKeys.end());
                            dataSource.Keys = MakeSharedRange(prefixKeys, rowBuffer);
                            dataSource.Schema = schema;
                        }

                        // COMPAT(lukyan): Use ordered read without modification of protocol
                        subquery->Limit = std::numeric_limits<i64>::max() - 1;

                        YT_LOG_DEBUG("Evaluating remote subquery (SubqueryId: %v)", subquery->Id);

                        auto pipe = New<NTableClient::TSchemafulPipe>();

                        auto asyncResult = remoteExecutor->Execute(
                            subquery,
                            ExternalCGInfo_,
                            std::move(dataSource),
                            pipe->GetWriter(),
                            ChunkReadOptions_,
                            remoteOptions);

                        asyncResult.Subscribe(BIND([pipe] (const TErrorOr<TQueryStatistics>& error) {
                            if (!error.IsOK()) {
                                pipe->Fail(error);
                            }
                        }));

                        asyncSubqueryResults->push_back(asyncResult);

                        return [
                            reader = pipe->GetReader()
                        ] (std::vector<TRow> keys, TRowBufferPtr permanentBuffer) {
                            return reader;
                        };
                    } else {
                        return [
                            asyncSubqueryResults,
                            remoteExecutor,
                            subquery,
                            joinClause,
                            remoteOptions,
                            this,
                            this_ = MakeStrong(this)
                        ] (std::vector<TRow> keys, TRowBufferPtr permanentBuffer) {
                            TDataSource dataSource;
                            TQueryPtr foreignQuery;
                            std::tie(foreignQuery, dataSource) = GetForeignQuery(
                                subquery,
                                joinClause,
                                std::move(keys),
                                permanentBuffer);

                            YT_LOG_DEBUG("Evaluating remote subquery (SubqueryId: %v)", foreignQuery->Id);

                            auto pipe = New<NTableClient::TSchemafulPipe>();

                            auto asyncResult = remoteExecutor->Execute(
                                foreignQuery,
                                ExternalCGInfo_,
                                std::move(dataSource),
                                pipe->GetWriter(),
                                ChunkReadOptions_,
                                remoteOptions);

                            asyncResult.Subscribe(BIND([pipe] (const TErrorOr<TQueryStatistics>& error) {
                                if (!error.IsOK()) {
                                    pipe->Fail(error);
                                }
                            }));

                            asyncSubqueryResults->push_back(asyncResult);

                            return pipe->GetReader();
                        };
                    }
                };

                auto mergingReader = subreaderCreators[index]();

                YT_LOG_DEBUG("Evaluating subquery (SubqueryId: %v)", subquery->Id);

                auto pipe = New<TSchemafulPipe>();

                auto asyncStatistics = BIND(&IEvaluator::Run, Evaluator_)
                    .AsyncVia(Invoker_)
                    .Run(
                        subquery,
                        mergingReader,
                        pipe->GetWriter(),
                        foreignProfileCallback,
                        functionGenerators,
                        aggregateGenerators,
                        MemoryChunkProvider_,
                        QueryOptions_);

                asyncStatistics = asyncStatistics.Apply(BIND([
                    =,
                    this_ = MakeStrong(this)
                ] (const TErrorOr<TQueryStatistics>& result) -> TFuture<TQueryStatistics>
                {
                    if (!result.IsOK()) {
                        pipe->Fail(result);
                        YT_LOG_DEBUG(result, "Failed evaluating subquery (SubqueryId: %v)", subquery->Id);
                        return MakeFuture(result);
                    } else {
                        TQueryStatistics statistics = result.Value();

                        return AllSucceeded(*asyncSubqueryResults)
                        .Apply(BIND([
                            =,
                            this_ = MakeStrong(this)
                        ] (const std::vector<TQueryStatistics>& subqueryResults) mutable {
                            for (const auto& subqueryResult : subqueryResults) {
                                YT_LOG_DEBUG("Remote subquery statistics %v", subqueryResult);
                                statistics.AddInnerStatistics(subqueryResult);
                            }
                            return statistics;
                        }));
                    }
                }));

                return std::make_pair(pipe->GetReader(), asyncStatistics);
            },
            [&] (const TConstFrontQueryPtr& topQuery, const ISchemafulUnversionedReaderPtr& reader, const IUnversionedRowsetWriterPtr& writer) {
                YT_LOG_DEBUG("Evaluating top query (TopQueryId: %v)", topQuery->Id);
                auto result = Evaluator_->Run(
                    topQuery,
                    std::move(reader),
                    std::move(writer),
                    nullptr,
                    functionGenerators,
                    aggregateGenerators,
                    MemoryChunkProvider_,
                    QueryOptions_);
                YT_LOG_DEBUG("Finished evaluating top query (TopQueryId: %v)", topQuery->Id);
                return result;
            });
    }

    TQueryStatistics DoExecute()
    {
        auto statistics = DoExecuteImpl();

        auto counters = TabletSnapshots_.GetTableProfiler()->GetSelectCpuCounters(GetProfilingUser(Identity_));

        auto cpuTime = statistics.SyncTime;
        for (const auto& innerStatistics : statistics.InnerStatistics) {
            cpuTime += innerStatistics.SyncTime;
        }

        counters->CpuTime.Add(cpuTime);
        counters->ChunkReaderStatisticsCounters.Increment(ChunkReadOptions_.ChunkReaderStatistics);

        return statistics;
    }

    TQueryStatistics DoExecuteImpl()
    {
        YT_LOG_DEBUG("Classifying data sources into ranges and lookup keys");

        std::vector<TDataSource> dataSourcesByTablet;

        auto rowBuffer = New<TRowBuffer>(TQuerySubexecutorBufferTag(), MemoryChunkProvider_);

        auto keySize = Query_->Schema.Original->GetKeyColumnCount();

        std::vector<EValueType> keySchema;
        for (size_t index = 0; index < keySize; ++index) {
            keySchema.push_back(Query_->Schema.Original->Columns()[index].GetPhysicalType());
        }

        bool hasRanges = false;
        for (const auto& source : DataSources_) {
            for (const auto& range : source.Ranges) {
                auto lowerBound = range.first;
                auto upperBound = range.second;

                if (source.LookupSupported &&
                    keySize == lowerBound.GetCount() &&
                    keySize + 1 == upperBound.GetCount() &&
                    upperBound[keySize].Type == EValueType::Max &&
                    CompareRows(lowerBound.Begin(), lowerBound.End(), upperBound.Begin(), upperBound.Begin() + keySize) == 0)
                {
                    continue;
                }

                hasRanges = true;
                break;
            }
        }

        size_t rangesCount = 0;
        for (const auto& source : DataSources_) {
            TRowRanges rowRanges;
            std::vector<TRow> keys;

            auto pushRanges = [&] {
                if (!rowRanges.empty()) {
                    rangesCount += rowRanges.size();
                    dataSourcesByTablet.push_back(TDataSource{
                        .ObjectId = source.ObjectId,
                        .CellId = source.CellId,
                        .KeyWidth = source.KeyWidth,
                        .Ranges = MakeSharedRange(std::move(rowRanges), source.Ranges.GetHolder(), rowBuffer),
                        .LookupSupported = source.LookupSupported
                    });
                }
            };

            auto pushKeys = [&] {
                if (!keys.empty()) {
                    dataSourcesByTablet.push_back(TDataSource{
                        .ObjectId = source.ObjectId,
                        .CellId = source.CellId,
                        .KeyWidth = source.KeyWidth,
                        .Keys = MakeSharedRange(std::move(keys), source.Ranges.GetHolder()),
                        .Schema = keySchema,
                        .LookupSupported = source.LookupSupported
                    });
                }
            };

            for (const auto& range : source.Ranges) {
                auto lowerBound = range.first;
                auto upperBound = range.second;

                if (source.LookupSupported &&
                    !hasRanges &&
                    keySize == lowerBound.GetCount() &&
                    keySize + 1 == upperBound.GetCount() &&
                    upperBound[keySize].Type == EValueType::Max &&
                    CompareRows(lowerBound.Begin(), lowerBound.End(), upperBound.Begin(), upperBound.Begin() + keySize) == 0)
                {
                    pushRanges();
                    keys.push_back(lowerBound);
                } else {
                    pushKeys();
                    rowRanges.push_back(range);
                }
            }

            for (const auto& key : source.Keys) {
                auto rowSize = key.GetCount();
                if (source.LookupSupported &&
                    !hasRanges &&
                    keySize == key.GetCount())
                {
                    pushRanges();
                    keys.push_back(key);
                } else {
                    pushKeys();
                    rowRanges.emplace_back(key, WidenKeySuccessor(key, rowSize, rowBuffer, false));
                }
            }
            pushRanges();
            pushKeys();
        }

        YT_LOG_DEBUG("Splitting ranges (RangeCount: %v)", rangesCount);

        auto splits = Split(std::move(dataSourcesByTablet), rowBuffer);

        std::vector<TRefiner> refiners;
        std::vector<TSubreaderCreator> subreaderCreators;
        std::vector<std::vector<TDataSource>> readRanges;

        auto processSplitsRanges = [&] (int beginIndex, int endIndex) {
            if (beginIndex == endIndex) {
                return;
            }

            std::vector<TDataSource> groupedSplit(splits.begin() + beginIndex, splits.begin() + endIndex);
            readRanges.push_back(groupedSplit);

            std::vector<TRowRange> keyRanges;
            for (const auto& dataRange : groupedSplit) {
                keyRanges.insert(keyRanges.end(), dataRange.Ranges.Begin(), dataRange.Ranges.End());
            }

            refiners.push_back([keyRanges = std::move(keyRanges), inferRanges = Query_->InferRanges] (
                const TConstExpressionPtr& expr,
                const TKeyColumns& keyColumns)
            {
                if (inferRanges) {
                    return EliminatePredicate(keyRanges, expr, keyColumns);
                } else {
                    return expr;
                }
            });

            subreaderCreators.push_back([=, groupedSplit = std::move(groupedSplit)] () {
                size_t rangesCount = std::accumulate(
                    groupedSplit.begin(),
                    groupedSplit.end(),
                    0,
                    [] (size_t sum, const TDataSource& element) {
                        return sum + element.Ranges.Size();
                    });
                YT_LOG_DEBUG("Generating reader for %v splits from %v ranges",
                    groupedSplit.size(),
                    rangesCount);

                LogSplits(groupedSplit);

                auto bottomSplitReaderGenerator = [
                    =,
                    this_ = MakeStrong(this),
                    groupedSplit = std::move(groupedSplit),
                    index = 0
                ] () mutable -> ISchemafulUnversionedReaderPtr {
                    if (index == groupedSplit.size()) {
                        return nullptr;
                    }

                    const auto& group = groupedSplit[index++];
                    return GetMultipleRangesReader(group.ObjectId, group.Ranges);
                };

                return CreatePrefetchingOrderedSchemafulReader(std::move(bottomSplitReaderGenerator));
            });
        };

        bool regroupByTablets = Query_->GroupClause && Query_->GroupClause->CommonPrefixWithPrimaryKey > 0;

        auto regroupAndProcessSplitsRanges = [&] (int beginIndex, int endIndex) {
            if (!regroupByTablets) {
                processSplitsRanges(beginIndex, endIndex);
                return;
            }
            size_t lastOffset = beginIndex;
            for (size_t index = beginIndex; index < endIndex; ++index) {
                if (index > lastOffset && splits[index].ObjectId != splits[lastOffset].ObjectId) {
                    processSplitsRanges(lastOffset, index);
                    lastOffset = index;
                }
            }
            processSplitsRanges(lastOffset, endIndex);
        };

        auto processSplitKeys = [&] (int index) {
            readRanges.push_back({splits[index]});

            auto tabletId = splits[index].ObjectId;
            const auto& keys = splits[index].Keys;

            refiners.push_back([keys, inferRanges = Query_->InferRanges] (
                const TConstExpressionPtr& expr,
                const TKeyColumns& keyColumns)
            {
                if (inferRanges) {
                    return EliminatePredicate(keys, expr, keyColumns);
                } else {
                    return expr;
                }
            });
            subreaderCreators.push_back([=, keys = std::move(keys)] {
                return GetTabletReader(tabletId, keys);
            });
        };

        int splitCount = splits.size();
        auto maxSubqueries = std::min({QueryOptions_.MaxSubqueries, Config_->MaxSubqueries, splitCount});
        int splitOffset = 0;
        int queryIndex = 1;
        int nextSplitOffset = queryIndex * splitCount / maxSubqueries;
        for (size_t splitIndex = 0; splitIndex < splitCount;) {
            if (splits[splitIndex].Keys) {
                regroupAndProcessSplitsRanges(splitOffset, splitIndex);
                processSplitKeys(splitIndex);
                splitOffset = ++splitIndex;
            } else {
                ++splitIndex;
            }

            if (splitIndex == nextSplitOffset) {
                regroupAndProcessSplitsRanges(splitOffset, splitIndex);
                splitOffset = splitIndex;
                ++queryIndex;
                nextSplitOffset = queryIndex * splitCount / maxSubqueries;
            }
        }

        YT_VERIFY(splitOffset == splitCount);

        return DoCoordinateAndExecute(
            std::move(refiners),
            std::move(subreaderCreators),
            std::move(readRanges));
    }

    std::vector<TDataSource> Split(std::vector<TDataSource> dataSourcesByTablet, TRowBufferPtr rowBuffer)
    {
        std::vector<TDataSource> groupedSplits;

        bool isSortedTable = false;

        for (auto& tabletIdRange : dataSourcesByTablet) {
            auto tabletId = tabletIdRange.ObjectId;
            auto cellId = tabletIdRange.CellId;
            auto& ranges = tabletIdRange.Ranges;

            auto tabletSnapshot = TabletSnapshots_.GetCachedTabletSnapshot(tabletId);

            YT_VERIFY(tabletIdRange.Keys.Empty() != ranges.Empty());

            if (!tabletSnapshot->TableSchema->IsSorted() || ranges.Empty()) {
                groupedSplits.push_back(tabletIdRange);
                continue;
            }

            isSortedTable = true;

            for (auto it = ranges.begin(), itEnd = ranges.end(); it + 1 < itEnd; ++it) {
                YT_QL_CHECK(it->second <= (it + 1)->first);
            }

            const auto& partitions = tabletSnapshot->PartitionList;
            YT_VERIFY(!partitions.empty());

            auto splits = SplitTablet(
                MakeRange(partitions),
                ranges,
                rowBuffer,
                Config_->MaxSubsplitsPerTablet,
                QueryOptions_.VerboseLogging,
                Logger);

            for (const auto& split : splits) {
                groupedSplits.push_back(TDataSource{
                    .ObjectId = tabletId,
                    .CellId = cellId,
                    .KeyWidth = tabletIdRange.KeyWidth,
                    .Ranges = split,
                    .LookupSupported = tabletIdRange.LookupSupported
                });
            }
        }

        if (isSortedTable) {
            for (const auto& split : groupedSplits) {
                for (auto it = split.Ranges.begin(), itEnd = split.Ranges.end(); it + 1 < itEnd; ++it) {
                    YT_QL_CHECK(it->second <= (it + 1)->first);
                }
            }

            for (auto it = groupedSplits.begin(), itEnd = groupedSplits.end(); it + 1 < itEnd; ++it) {
                const auto& lhs = *it;
                const auto& rhs = *(it + 1);

                const auto& lhsValue = lhs.Ranges ? lhs.Ranges.Back().second : lhs.Keys.Back();
                const auto& rhsValue = rhs.Ranges ? rhs.Ranges.Front().first : rhs.Keys.Front();

                YT_QL_CHECK(lhsValue <= rhsValue);
            }
        }

        return groupedSplits;
    }

    ISchemafulUnversionedReaderPtr GetMultipleRangesReader(
        TTabletId tabletId,
        const TSharedRange<TRowRange>& bounds)
    {
        auto tabletSnapshot = TabletSnapshots_.GetCachedTabletSnapshot(tabletId);
        auto columnFilter = GetColumnFilter(*Query_->GetReadSchema(), *tabletSnapshot->QuerySchema);
        auto tableProfiler = tabletSnapshot->TableProfiler;
        auto userTag = GetProfilingUser(Identity_);

        ISchemafulUnversionedReaderPtr reader;

        if (!tabletSnapshot->TableSchema->IsSorted()) {
            auto bottomSplitReaderGenerator = [
                =,
                this_ = MakeStrong(this),
                tabletSnapshot = std::move(tabletSnapshot),
                bounds = std::move(bounds),
                index = 0
            ] () mutable -> ISchemafulUnversionedReaderPtr {
                if (index == bounds.Size()) {
                    return nullptr;
                }

                const auto& range = bounds[index++];

                return CreateSchemafulOrderedTabletReader(
                    tabletSnapshot,
                    columnFilter,
                    TLegacyOwningKey(range.first),
                    TLegacyOwningKey(range.second),
                    QueryOptions_.Timestamp,
                    ChunkReadOptions_,
                    ETabletDistributedThrottlerKind::Select);
            };

            reader = CreateUnorderedSchemafulReader(std::move(bottomSplitReaderGenerator), 1);
        } else {
            reader = CreateSchemafulSortedTabletReader(
                std::move(tabletSnapshot),
                columnFilter,
                bounds,
                QueryOptions_.Timestamp,
                ChunkReadOptions_,
                ETabletDistributedThrottlerKind::Select);
        }

        return New<TProfilingReaderWrapper>(reader, *tableProfiler->GetSelectReadCounters(userTag));
    }

    ISchemafulUnversionedReaderPtr GetTabletReader(
        TTabletId tabletId,
        const TSharedRange<TRow>& keys)
    {
        auto tabletSnapshot = TabletSnapshots_.GetCachedTabletSnapshot(tabletId);
        auto columnFilter = GetColumnFilter(*Query_->GetReadSchema(), *tabletSnapshot->QuerySchema);
        auto tableProfiler = tabletSnapshot->TableProfiler;
        auto userTag = GetProfilingUser(Identity_);

        auto reader = CreateSchemafulLookupTabletReader(
            std::move(tabletSnapshot),
            columnFilter,
            keys,
            QueryOptions_.Timestamp,
            ChunkReadOptions_,
            ETabletDistributedThrottlerKind::Select);

        return New<TProfilingReaderWrapper>(reader, *tableProfiler->GetSelectReadCounters(userTag));
    }
};

////////////////////////////////////////////////////////////////////////////////

TQueryStatistics ExecuteSubquery(
    TQueryAgentConfigPtr config,
    TFunctionImplCachePtr functionImplCache,
    TBootstrap* const bootstrap,
    IEvaluatorPtr evaluator,
    TConstQueryPtr query,
    TConstExternalCGInfoPtr externalCGInfo,
    std::vector<TDataSource> dataSources,
    IUnversionedRowsetWriterPtr writer,
    IMemoryChunkProviderPtr memoryChunkProvider,
    IInvokerPtr invoker,
    const TClientChunkReadOptions& chunkReadOptions,
    const TQueryOptions& queryOptions,
    TServiceProfilerGuard& profilerGuard)
{
    ValidateReadTimestamp(queryOptions.Timestamp);

    auto execution = New<TQueryExecution>(
        config,
        functionImplCache,
        bootstrap,
        evaluator,
        std::move(query),
        std::move(externalCGInfo),
        std::move(dataSources),
        std::move(writer),
        std::move(memoryChunkProvider),
        invoker,
        chunkReadOptions,
        queryOptions);

    return execution->Execute(profilerGuard);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueryAgent
