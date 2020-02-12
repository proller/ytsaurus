#include "admin.h"
#include "config.h"
#include "connection.h"
#include "client.h"
#include "admin.h"
#include "transaction_participant.h"
#include "transaction.h"
#include "private.h"

#include <yt/ytlib/cell_master_client/cell_directory.h>
#include <yt/ytlib/cell_master_client/cell_directory_synchronizer.h>

#include <yt/ytlib/chunk_client/client_block_cache.h>

#include <yt/ytlib/hive/cell_directory.h>
#include <yt/ytlib/hive/cell_directory_synchronizer.h>
#include <yt/ytlib/hive/cell_tracker.h>
#include <yt/ytlib/hive/cluster_directory.h>
#include <yt/ytlib/hive/cluster_directory_synchronizer.h>
#include <yt/ytlib/hive/hive_service_proxy.h>

#include <yt/ytlib/hydra/peer_channel.h>

#include <yt/ytlib/query_client/column_evaluator.h>
#include <yt/ytlib/query_client/evaluator.h>
#include <yt/ytlib/query_client/functions_cache.h>

#include <yt/ytlib/node_tracker_client/node_directory_synchronizer.h>
#include <yt/ytlib/node_tracker_client/master_cache_synchronizer.h>

#include <yt/ytlib/job_prober_client/job_node_descriptor_cache.h>

#include <yt/ytlib/object_client/object_service_proxy.h>

#include <yt/ytlib/scheduler/scheduler_channel.h>

#include <yt/client/tablet_client/table_mount_cache.h>
#include <yt/ytlib/tablet_client/native_table_mount_cache.h>

#include <yt/ytlib/transaction_client/config.h>
#include <yt/client/transaction_client/remote_timestamp_provider.h>

#include <yt/client/api/sticky_transaction_pool.h>

#include <yt/client/object_client/helpers.h>

#include <yt/core/profiling/profiler.h>
#include <yt/core/profiling/profile_manager.h>

#include <yt/core/concurrency/action_queue.h>
#include <yt/core/concurrency/thread_pool.h>
#include <yt/core/concurrency/lease_manager.h>
#include <yt/core/concurrency/periodic_executor.h>

#include <yt/core/ytree/fluent.h>

#include <yt/core/rpc/bus/channel.h>
#include <yt/core/rpc/caching_channel_factory.h>

#include <yt/core/rpc/retrying_channel.h>
#include <yt/core/rpc/roaming_channel.h>
#include <yt/core/rpc/reconfigurable_roaming_channel_provider.h>

namespace NYT::NApi::NNative {

using namespace NConcurrency;
using namespace NRpc;
using namespace NHiveClient;
using namespace NChunkClient;
using namespace NTabletClient;
using namespace NTransactionClient;
using namespace NObjectClient;
using namespace NQueryClient;
using namespace NHydra;
using namespace NNodeTrackerClient;
using namespace NJobProberClient;
using namespace NScheduler;
using namespace NProfiling;
using namespace NYson;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

class TConnection
    : public IConnection
{
public:
    TConnection(
        TConnectionConfigPtr config,
        const TConnectionOptions& options)
        : Config_(std::move(config))
        , Options_(options)
        , Logger(NLogging::TLogger(ApiLogger)
            .AddTag("PrimaryCellTag: %v, ConnectionId: %v, ConnectionName: %v",
                CellTagFromId(Config_->PrimaryMaster->CellId),
                TGuid::Create(),
                Config_->Name))
        , CachingChannelFactory_(CreateCachingChannelFactory(NRpc::NBus::CreateBusChannelFactory(Config_->BusClient)))
        , ChannelFactory_(CachingChannelFactory_)
        , Profiler_("/connection", {TProfileManager::Get()->RegisterTag("connection_name", Config_->Name)})
    { }

    void Initialize()
    {
        if (Config_->ThreadPoolSize) {
            ThreadPool_ = New<TThreadPool>(*Config_->ThreadPoolSize, "Connection");
        }

        if (Config_->MasterCache && Config_->MasterCache->EnableMasterCacheDiscovery) {
            MasterCacheSynchronizer_ = New<TMasterCacheSynchronizer>(
                Config_->MasterCache->MasterCacheDiscoveryPeriod,
                MakeWeak(this));
        }

        TerminateIdleChannelsExecutor_ = New<TPeriodicExecutor>(
            GetInvoker(),
            BIND(&ICachingChannelFactory::TerminateIdleChannels, MakeWeak(CachingChannelFactory_), Config_->IdleChannelTtl),
            Config_->IdleChannelTtl);
        TerminateIdleChannelsExecutor_->Start();

        MasterCellDirectory_ = New<NCellMasterClient::TCellDirectory>(
            Config_,
            Options_,
            ChannelFactory_,
            MasterCacheSynchronizer_,
            Logger);
        MasterCellDirectorySynchronizer_ = New<NCellMasterClient::TCellDirectorySynchronizer>(
            Config_->MasterCellDirectorySynchronizer,
            MasterCellDirectory_);
        MasterCellDirectorySynchronizer_->Start();

        auto timestampProviderConfig = Config_->TimestampProvider;
        if (!timestampProviderConfig) {
            // Use masters for timestamp generation.
            timestampProviderConfig = New<TRemoteTimestampProviderConfig>();
            timestampProviderConfig->Addresses = Config_->PrimaryMaster->Addresses;
            timestampProviderConfig->RpcTimeout = Config_->PrimaryMaster->RpcTimeout;
            // TRetryingChannelConfig
            timestampProviderConfig->RetryBackoffTime = Config_->PrimaryMaster->RetryBackoffTime;
            timestampProviderConfig->RetryAttempts = Config_->PrimaryMaster->RetryAttempts;
            timestampProviderConfig->RetryTimeout = Config_->PrimaryMaster->RetryTimeout;
        }
        TimestampProvider_ = CreateRemoteTimestampProvider(
            timestampProviderConfig,
            ChannelFactory_);

        SchedulerChannel_ = CreateSchedulerChannel(
            Config_->Scheduler,
            ChannelFactory_,
            GetMasterChannelOrThrow(EMasterChannelKind::Leader),
            GetNetworks());

        JobNodeDescriptorCache_ = New<TJobNodeDescriptorCache>(
            Config_->JobNodeDescriptorCache,
            SchedulerChannel_);

        ClusterDirectory_ = New<TClusterDirectory>();
        ClusterDirectorySynchronizer_ = New<TClusterDirectorySynchronizer>(
            Config_->ClusterDirectorySynchronizer,
            this,
            ClusterDirectory_);

        CellDirectory_ = New<NHiveClient::TCellDirectory>(
            Config_->CellDirectory,
            ChannelFactory_,
            GetNetworks(),
            Logger);
        CellDirectory_->ReconfigureCell(Config_->PrimaryMaster);
        for (const auto& cellConfig : Config_->SecondaryMasters) {
            CellDirectory_->ReconfigureCell(cellConfig);
        }

        CellDirectorySynchronizer_ = New<NHiveClient::TCellDirectorySynchronizer>(
            Config_->CellDirectorySynchronizer,
            CellDirectory_,
            GetPrimaryMasterCellId(),
            Logger);
        DownedCellTracker_ = New<TCellTracker>();

        BlockCache_ = CreateClientBlockCache(
            Config_->BlockCache,
            EBlockType::CompressedData|EBlockType::UncompressedData,
            Profiler_.AppendPath("/block_cache"));

        TableMountCache_ = CreateNativeTableMountCache(
            Config_->TableMountCache,
            this,
            CellDirectory_,
            Logger);

        QueryEvaluator_ = New<TEvaluator>(Config_->QueryEvaluator);
        ColumnEvaluatorCache_ = New<TColumnEvaluatorCache>(Config_->ColumnEvaluatorCache);

        NodeDirectory_ = New<TNodeDirectory>();
        NodeDirectorySynchronizer_ = New<TNodeDirectorySynchronizer>(
            Config_->NodeDirectorySynchronizer,
            MakeStrong(this),
            NodeDirectory_);

        if (MasterCacheSynchronizer_) {
            MasterCacheSynchronizer_->Start();
        }
    }

    // IConnection implementation.

    virtual TCellTag GetCellTag() override
    {
        return GetPrimaryMasterCellTag();
    }

    virtual const ITableMountCachePtr& GetTableMountCache() override
    {
        return TableMountCache_;
    }

    virtual const ITimestampProviderPtr& GetTimestampProvider() override
    {
        return TimestampProvider_;
    }

    virtual const TJobNodeDescriptorCachePtr& GetJobNodeDescriptorCache() override
    {
        return JobNodeDescriptorCache_;
    }

    virtual IInvokerPtr GetInvoker() override
    {
        return ThreadPool_ ? ThreadPool_->GetInvoker() : GetCurrentInvoker();
    }

    virtual IAdminPtr CreateAdmin(const TAdminOptions& options) override
    {
        return NNative::CreateAdmin(this, options);
    }

    virtual NApi::IClientPtr CreateClient(const TClientOptions& options) override
    {
        return NNative::CreateClient(this, options);
    }

    virtual void ClearMetadataCaches() override
    {
        TableMountCache_->Clear();
    }

    // NNative::IConnection implementation.

    virtual const TConnectionConfigPtr& GetConfig() override
    {
        return Config_;
    }

    virtual const TNetworkPreferenceList& GetNetworks() const override
    {
        return Config_->Networks ? *Config_->Networks : DefaultNetworkPreferences;
    }

    virtual TCellId GetPrimaryMasterCellId() const override
    {
        return MasterCellDirectory_->GetPrimaryMasterCellId();
    }

    virtual TCellTag GetPrimaryMasterCellTag() const override
    {
        return MasterCellDirectory_->GetPrimaryMasterCellTag();
    }

    virtual const TCellTagList& GetSecondaryMasterCellTags() const override
    {
        return MasterCellDirectory_->GetSecondaryMasterCellTags();
    }

    virtual TCellId GetMasterCellId(TCellTag cellTag) const override
    {
        return ReplaceCellTagInId(GetPrimaryMasterCellId(), cellTag);
    }

    virtual IChannelPtr GetMasterChannelOrThrow(
        EMasterChannelKind kind,
        TCellTag cellTag = PrimaryMasterCellTag) override
    {
        return MasterCellDirectory_->GetMasterChannelOrThrow(kind, cellTag);
    }

    virtual IChannelPtr GetMasterChannelOrThrow(
        EMasterChannelKind kind,
        TCellId cellId) override
    {
        return MasterCellDirectory_->GetMasterChannelOrThrow(kind, cellId);
    }

    virtual const IChannelPtr& GetSchedulerChannel() override
    {
        return SchedulerChannel_;
    }

    virtual const IChannelFactoryPtr& GetChannelFactory() override
    {
        return ChannelFactory_;
    }

    virtual const IBlockCachePtr& GetBlockCache() override
    {
        return BlockCache_;
    }

    virtual const TEvaluatorPtr& GetQueryEvaluator() override
    {
        return QueryEvaluator_;
    }

    virtual const TColumnEvaluatorCachePtr& GetColumnEvaluatorCache() override
    {
        return ColumnEvaluatorCache_;
    }

    virtual const NCellMasterClient::TCellDirectoryPtr& GetMasterCellDirectory() override
    {
        return MasterCellDirectory_;
    }

    virtual const NCellMasterClient::TCellDirectorySynchronizerPtr& GetMasterCellDirectorySynchronizer() override
    {
        return MasterCellDirectorySynchronizer_;
    }

    virtual const NHiveClient::TCellDirectoryPtr& GetCellDirectory() override
    {
        return CellDirectory_;
    }

    virtual const NHiveClient::TCellDirectorySynchronizerPtr& GetCellDirectorySynchronizer() override
    {
        return CellDirectorySynchronizer_;
    }

    virtual const TNodeDirectoryPtr& GetNodeDirectory() override
    {
        NodeDirectorySynchronizer_->Start();
        return NodeDirectory_;
    }

    virtual const TNodeDirectorySynchronizerPtr& GetNodeDirectorySynchronizer() override
    {
        NodeDirectorySynchronizer_->Start();
        return NodeDirectorySynchronizer_;
    }

    virtual const TCellTrackerPtr& GetDownedCellTracker() override
    {
        return DownedCellTracker_;
    }

    virtual const NHiveClient::TClusterDirectoryPtr& GetClusterDirectory() override
    {
        return ClusterDirectory_;
    }

    virtual const NHiveClient::TClusterDirectorySynchronizerPtr& GetClusterDirectorySynchronizer() override
    {
        return ClusterDirectorySynchronizer_;
    }


    virtual IClientPtr CreateNativeClient(const TClientOptions& options) override
    {
        return NNative::CreateClient(this, options);
    }

    virtual NHiveClient::ITransactionParticipantPtr CreateTransactionParticipant(
        TCellId cellId,
        const TTransactionParticipantOptions& options) override
    {
        // For tablet writes, manual sync is not needed since Table Mount Cache
        // is responsible for populating cell directory. Transaction participants,
        // on the other hand, have no other way to keep cell directory up-to-date.
        CellDirectorySynchronizer_->Start();
        return NNative::CreateTransactionParticipant(
            CellDirectory_,
            CellDirectorySynchronizer_,
            TimestampProvider_,
            this,
            cellId,
            options);
    }

    virtual IYPathServicePtr GetOrchidService() override
    {
        auto producer = BIND(&TConnection::BuildOrchid, MakeStrong(this));
        return IYPathService::FromProducer(producer);
    }

    virtual void Terminate() override
    {
        Terminated_ = true;

        ClusterDirectory_->Clear();
        ClusterDirectorySynchronizer_->Stop();

        CellDirectory_->Clear();
        CellDirectorySynchronizer_->Stop();

        NodeDirectorySynchronizer_->Stop();

        if (MasterCacheSynchronizer_) {
            MasterCacheSynchronizer_->Stop();
        }
    }

    virtual bool IsTerminated() override
    {
        return Terminated_;
    }

    virtual TFuture<void> SyncHiveCellWithOthers(
        const std::vector<TCellId>& srcCellIds,
        TCellId dstCellId) override
    {
        YT_LOG_DEBUG("Started synchronizing Hive cell with others (SrcCellIds: %v, DstCellId: %v)",
            srcCellIds,
            dstCellId);

        auto channel = CellDirectory_->GetChannelOrThrow(dstCellId);
        THiveServiceProxy proxy(std::move(channel));

        auto req = proxy.SyncWithOthers();
        req->SetTimeout(Config_->HiveSyncRpcTimeout);
        ToProto(req->mutable_src_cell_ids(), srcCellIds);

        return req->Invoke()
            .Apply(BIND([=] (const THiveServiceProxy::TErrorOrRspSyncWithOthersPtr& rspOrError) {
                THROW_ERROR_EXCEPTION_IF_FAILED(
                    rspOrError,
                    "Error synchronizing Hive cell %v with %v",
                    dstCellId,
                    srcCellIds);
                YT_LOG_DEBUG("Finished synchronizing Hive cell with others (SrcCellIds: %v, DstCellId: %v)",
                    srcCellIds,
                    dstCellId);
            }));
    }

private:
    const TConnectionConfigPtr Config_;
    const TConnectionOptions Options_;

    const NLogging::TLogger Logger;

    // These two fields hold reference to the same object.
    const NRpc::ICachingChannelFactoryPtr CachingChannelFactory_;
    const NRpc::IChannelFactoryPtr ChannelFactory_;
    TPeriodicExecutorPtr TerminateIdleChannelsExecutor_;

    // NB: there're also CellDirectory_ and CellDirectorySynchronizer_, which are completely different from these.
    NCellMasterClient::TCellDirectoryPtr MasterCellDirectory_;
    NCellMasterClient::TCellDirectorySynchronizerPtr MasterCellDirectorySynchronizer_;

    IChannelPtr SchedulerChannel_;
    IBlockCachePtr BlockCache_;
    ITableMountCachePtr TableMountCache_;
    ITimestampProviderPtr TimestampProvider_;
    TJobNodeDescriptorCachePtr JobNodeDescriptorCache_;
    TEvaluatorPtr QueryEvaluator_;
    TColumnEvaluatorCachePtr ColumnEvaluatorCache_;

    NHiveClient::TCellDirectoryPtr CellDirectory_;
    NHiveClient::TCellDirectorySynchronizerPtr CellDirectorySynchronizer_;
    TCellTrackerPtr DownedCellTracker_;

    TClusterDirectoryPtr ClusterDirectory_;
    TClusterDirectorySynchronizerPtr ClusterDirectorySynchronizer_;

    TMasterCacheSynchronizerPtr MasterCacheSynchronizer_;

    TNodeDirectoryPtr NodeDirectory_;
    TNodeDirectorySynchronizerPtr NodeDirectorySynchronizer_;

    TThreadPoolPtr ThreadPool_;

    TProfiler Profiler_;

    std::atomic<bool> Terminated_ = {false};

    void BuildOrchid(IYsonConsumer* consumer)
    {
        bool hasMasterCache = static_cast<bool>(Config_->MasterCache);

        BuildYsonFluently(consumer)
            .BeginMap()
                .Item("master_cache")
                    .BeginMap()
                        .Item("enabled").Value(hasMasterCache)
                        .DoIf(hasMasterCache, [this] (auto fluent) {
                            bool dynamic = Config_->MasterCache->EnableMasterCacheDiscovery;
                            const auto& addresses = dynamic ? MasterCacheSynchronizer_->GetAddresses() : Config_->MasterCache->Addresses;
                            fluent
                                .Item("dynamic").Value(dynamic)
                                .Item("addresses").List(addresses);
                        })
                    .EndMap()
            .EndMap();
    }
};

IConnectionPtr CreateConnection(
    TConnectionConfigPtr config,
    const TConnectionOptions& options)
{
    auto connection = New<TConnection>(config, options);
    connection->Initialize();
    return connection;
}

////////////////////////////////////////////////////////////////////////////////

IConnectionPtr FindRemoteConnection(
    const IConnectionPtr& connection,
    TCellTag cellTag)
{
    if (cellTag == connection->GetCellTag()) {
        return connection;
    }

    auto remoteConnection = connection->GetClusterDirectory()->FindConnection(cellTag);
    if (!remoteConnection) {
        return nullptr;
    }

    return dynamic_cast<NNative::IConnection*>(remoteConnection.Get());
}

IConnectionPtr GetRemoteConnectionOrThrow(
    const IConnectionPtr& connection,
    TCellTag cellTag)
{
    auto remoteConnection = FindRemoteConnection(connection, cellTag);
    if (!remoteConnection) {
        THROW_ERROR_EXCEPTION("Cannot find cluster with cell tag %v", cellTag);
    }
    return remoteConnection;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NNative
