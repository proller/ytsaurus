#include "world_initializer.h"
#include "private.h"
#include "config.h"
#include "hydra_facade.h"

#include <yt/server/cell_master/bootstrap.h>

#include <yt/server/cypress_server/cypress_manager.h>
#include <yt/server/cypress_server/node_detail.h>

#include <yt/server/hive/transaction_supervisor.h>

#include <yt/server/security_server/acl.h>
#include <yt/server/security_server/group.h>
#include <yt/server/security_server/security_manager.h>

#include <yt/ytlib/cypress_client/cypress_ypath_proxy.h>
#include <yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/ytlib/election/cell_manager.h>

#include <yt/ytlib/object_client/helpers.h>
#include <yt/ytlib/object_client/master_ypath_proxy.h>
#include <yt/ytlib/object_client/object_service_proxy.h>

#include <yt/ytlib/transaction_client/transaction_ypath.pb.h>

#include <yt/core/concurrency/scheduler.h>

#include <yt/core/logging/log.h>

#include <yt/core/misc/collection_helpers.h>

#include <yt/core/ypath/token.h>

#include <yt/core/ytree/ypath_client.h>
#include <yt/core/ytree/ypath_proxy.h>

#include <yt/server/transaction_server/transaction_manager.h>

namespace NYT {
namespace NCellMaster {

using namespace NConcurrency;
using namespace NYTree;
using namespace NYson;
using namespace NYPath;
using namespace NCypressServer;
using namespace NCypressClient;
using namespace NTransactionClient;
using namespace NTransactionClient::NProto;
using namespace NHive;
using namespace NHive::NProto;
using namespace NObjectClient;
using namespace NObjectServer;
using namespace NSecurityServer;

using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = CellMasterLogger;
static const auto InitRetryPeriod = TDuration::Seconds(3);
static const auto InitTransactionTimeout = TDuration::Seconds(60);

////////////////////////////////////////////////////////////////////////////////

class TWorldInitializer::TImpl
    : public TRefCounted
{
public:
    TImpl(
        TCellMasterConfigPtr config,
        TBootstrap* bootstrap)
        : Config_(config)
        , Bootstrap_(bootstrap)
    {
        YCHECK(Config_);
        YCHECK(Bootstrap_);

        auto hydraManager = Bootstrap_->GetHydraFacade()->GetHydraManager();
        hydraManager->SubscribeLeaderActive(BIND(&TImpl::OnLeaderActive, MakeWeak(this)));
    }


    bool CheckInitialized()
    {
        auto cypressManager = Bootstrap_->GetCypressManager();
        auto* root = dynamic_cast<TMapNode*>(cypressManager->GetRootNode());
        YCHECK(root);
        return !root->KeyToChild().empty();
    }

    bool CheckProvisionLock()
    {
        auto cypressManager = Bootstrap_->GetCypressManager();
        auto resolver = cypressManager->CreateResolver();
        auto sysNode = resolver->ResolvePath("//sys");
        return sysNode->Attributes().Get<bool>("provision_lock", false);
    }

private:
    const TCellMasterConfigPtr Config_;
    TBootstrap* const Bootstrap_;


    void OnLeaderActive()
    {
        // NB: Initialization cannot be carried out here since not all subsystems
        // are fully initialized yet.
        // We'll post an initialization callback to the automaton invoker instead.
        ScheduleInitialize();
    }

    void InitializeIfNeeded()
    {
        if (CheckInitialized()) {
            LOG_INFO("World is already initialized");
        } else {
            Initialize();
        }
    }

    void ScheduleInitialize(TDuration delay = TDuration::Zero())
    {
        TDelayedExecutor::Submit(
            BIND(&TImpl::InitializeIfNeeded, MakeStrong(this))
                .Via(Bootstrap_->GetHydraFacade()->GetEpochAutomatonInvoker()),
            delay);
    }

    void Initialize()
    {
        LOG_INFO("World initialization started");

        try {
            // Check for pre-existing transactions to avoid collisions with previous (failed)
            // initialization attempts.
            auto transactionManager = Bootstrap_->GetTransactionManager();
            if (transactionManager->Transactions().GetSize() > 0) {
                AbortTransactions();
                THROW_ERROR_EXCEPTION("World initialization aborted: transactions found");
            }

            auto objectManager = Bootstrap_->GetObjectManager();
            auto cypressManager = Bootstrap_->GetCypressManager();
            auto securityManager = Bootstrap_->GetSecurityManager();

            // All initialization will be happening within this transaction.
            auto transactionId = StartTransaction();

            CreateNode(
                "//sys",
                transactionId,
                EObjectType::SysNode,
                BuildYsonStringFluently()
                    .BeginMap()
                        .DoIf(Config_->EnableProvisionLock, [&] (TFluentMap fluent) {
                            fluent.Item("provision_lock").Value(true);
                        })
                    .EndMap());

            CreateNode(
                "//sys/schemas",
                transactionId,
                EObjectType::MapNode,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("opaque").Value(true)
                    .EndMap());

            for (auto type : objectManager->GetRegisteredTypes()) {
                if (HasSchema(type)) {
                    CreateNode(
                        "//sys/schemas/" + ToYPathLiteral(FormatEnum(type)),
                        transactionId,
                        EObjectType::Link,
                        BuildYsonStringFluently()
                            .BeginMap()
                                .Item("target_id").Value(objectManager->GetSchema(type)->GetId())
                            .EndMap());
                }
            }

            CreateNode(
                "//sys/scheduler",
                transactionId,
                EObjectType::MapNode,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("opaque").Value(true)
                    .EndMap());

            CreateNode(
                "//sys/scheduler/lock",
                transactionId,
                EObjectType::MapNode);

            CreateNode(
                "//sys/pools",
                transactionId,
                EObjectType::MapNode,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("opaque").Value(true)
                    .EndMap());

            CreateNode(
                "//sys/tokens",
                transactionId,
                EObjectType::Document,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("value").BeginMap()
                        .EndMap()
                    .EndMap());

            CreateNode(
                "//sys/clusters",
                transactionId,
                EObjectType::Document,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("value").BeginMap()
                        .EndMap()
                    .EndMap());

            CreateNode(
                "//sys/empty_yamr_table",
                transactionId,
                EObjectType::Table,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("external").Value(false)
                        .Item("schema")
                            .BeginAttributes()
                                .Item("strict").Value(true)
                            .EndAttributes()
                            .BeginList()
                                .Item()
                                    .BeginMap()
                                        .Item("name").Value("key")
                                        .Item("type").Value("string")
                                        .Item("sort_order").Value("ascending")
                                    .EndMap()
                                .Item()
                                    .BeginMap()
                                        .Item("name").Value("subkey")
                                        .Item("type").Value("string")
                                        .Item("sort_order").Value("ascending")
                                    .EndMap()
                                .Item()
                                    .BeginMap()
                                        .Item("name").Value("value")
                                        .Item("type").Value("string")
                                    .EndMap() 
                            .EndList()
                    .EndMap());

            CreateNode(
                "//sys/scheduler/instances",
                transactionId,
                EObjectType::MapNode,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("opaque").Value(true)
                    .EndMap());

            CreateNode(
                "//sys/scheduler/orchid",
                transactionId,
                EObjectType::Orchid);

            CreateNode(
                "//sys/scheduler/event_log",
                transactionId,
                EObjectType::Table,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("external").Value(false)
                    .EndMap());

            CreateNode(
                "//sys/operations",
                transactionId,
                EObjectType::MapNode,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("opaque").Value(true)
                    .EndMap());

            CreateNode(
                "//sys/proxies",
                transactionId,
                EObjectType::MapNode,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("opaque").Value(true)
                    .EndMap());

            CreateNode(
                "//sys/nodes",
                transactionId,
                EObjectType::ClusterNodeMap,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("opaque").Value(true)
                    .EndMap());

            CreateNode(
                "//sys/racks",
                transactionId,
                EObjectType::RackMap);

            auto createMasters = [&] (const TYPath& rootPath, NElection::TCellConfigPtr cellConfig) {
                for (const auto& peer : cellConfig->Peers) {
                    const auto& address = *peer.Address;
                    auto addressPath = "/" + ToYPathLiteral(address);

                    CreateNode(
                        rootPath + addressPath,
                        transactionId,
                        EObjectType::MapNode);

                    CreateNode(
                        rootPath + addressPath + "/orchid",
                        transactionId,
                        EObjectType::Orchid,
                        BuildYsonStringFluently()
                            .BeginMap()
                                .Item("remote_address").Value(address)
                            .EndMap());
                }
            };

            CreateNode(
                "//sys/primary_masters",
                transactionId,
                EObjectType::MapNode,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("opaque").Value(true)
                    .EndMap());

            createMasters("//sys/primary_masters", Config_->PrimaryMaster);

            CreateNode(
                "//sys/secondary_masters",
                transactionId,
                EObjectType::MapNode,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("opaque").Value(true)
                    .EndMap());

            for (auto cellConfig : Config_->SecondaryMasters) {
                auto cellTag = CellTagFromId(cellConfig->CellId);
                auto cellPath = "//sys/secondary_masters/" + ToYPathLiteral(cellTag);

                CreateNode(
                    cellPath,
                    transactionId,
                    EObjectType::MapNode);

                createMasters(cellPath, cellConfig);
            }

            CreateNode(
                "//sys/locks",
                transactionId,
                EObjectType::LockMap);

            CreateNode(
                "//sys/chunks",
                transactionId,
                EObjectType::ChunkMap);

            CreateNode(
                "//sys/lost_chunks",
                transactionId,
                EObjectType::LostChunkMap);

            CreateNode(
                "//sys/lost_vital_chunks",
                transactionId,
                EObjectType::LostVitalChunkMap);

            CreateNode(
                "//sys/overreplicated_chunks",
                transactionId,
                EObjectType::OverreplicatedChunkMap);

            CreateNode(
                "//sys/underreplicated_chunks",
                transactionId,
                EObjectType::UnderreplicatedChunkMap);

            CreateNode(
                "//sys/data_missing_chunks",
                transactionId,
                EObjectType::DataMissingChunkMap);

            CreateNode(
                "//sys/parity_missing_chunks",
                transactionId,
                EObjectType::ParityMissingChunkMap);

            CreateNode(
                "//sys/quorum_missing_chunks",
                transactionId,
                EObjectType::QuorumMissingChunkMap);

            CreateNode(
                "//sys/unsafely_placed_chunks",
                transactionId,
                EObjectType::UnsafelyPlacedChunkMap);

            CreateNode(
                "//sys/foreign_chunks",
                transactionId,
                EObjectType::ForeignChunkMap);

            CreateNode(
                "//sys/chunk_lists",
                transactionId,
                EObjectType::ChunkListMap);

            CreateNode(
                "//sys/transactions",
                transactionId,
                EObjectType::TransactionMap);

            CreateNode(
                "//sys/topmost_transactions",
                transactionId,
                EObjectType::TopmostTransactionMap);

            CreateNode(
                "//sys/accounts",
                transactionId,
                EObjectType::AccountMap);

            CreateNode(
                "//sys/users",
                transactionId,
                EObjectType::UserMap);

            CreateNode(
                "//sys/groups",
                transactionId,
                EObjectType::GroupMap);

            CreateNode(
                "//sys/tablet_cell_bundles",
                transactionId,
                EObjectType::TabletCellBundleMap);

            CreateNode(
                "//sys/tablet_cells",
                transactionId,
                EObjectType::MapNode,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("opaque").Value(true)
                    .EndMap());

            CreateNode(
                "//sys/tablets",
                transactionId,
                EObjectType::TabletMap);

            CreateNode(
                "//tmp",
                transactionId,
                EObjectType::MapNode,
                BuildYsonStringFluently()
                    .BeginMap()
                        .Item("opaque").Value(true)
                        .Item("account").Value("tmp")
                        .Item("acl").BeginList()
                            .Item().Value(TAccessControlEntry(
                                ESecurityAction::Allow,
                                securityManager->GetUsersGroup(),
                                EPermissionSet(EPermission::Read | EPermission::Write | EPermission::Remove)))
                        .EndList()
                    .EndMap());

            CommitTransaction(transactionId);

            LOG_INFO("World initialization completed");
        } catch (const std::exception& ex) {
            LOG_ERROR(ex, "World initialization failed");
            ScheduleInitialize(InitRetryPeriod);
        }
    }

    void AbortTransactions()
    {
        auto transactionManager = Bootstrap_->GetTransactionManager();
        auto transactionIds = ToObjectIds(GetValues(transactionManager->Transactions()));
        auto transactionSupervisor = Bootstrap_->GetTransactionSupervisor();
        for (const auto& transactionId : transactionIds) {
            transactionSupervisor->AbortTransaction(transactionId);
        }
    }

    TTransactionId StartTransaction()
    {
        auto service = Bootstrap_->GetObjectManager()->GetRootService();
        auto req = TMasterYPathProxy::CreateObject();
        req->set_type(static_cast<int>(EObjectType::Transaction));

        auto* requestExt = req->mutable_extensions()->MutableExtension(TTransactionCreationExt::transaction_creation_ext);
        requestExt->set_timeout(ToProto(InitTransactionTimeout));

        auto attributes = CreateEphemeralAttributes();
        attributes->Set("title", "World initialization");
        ToProto(req->mutable_object_attributes(), *attributes);

        auto rsp = WaitFor(ExecuteVerb(service, req))
            .ValueOrThrow();
        return FromProto<TTransactionId>(rsp->object_id());
    }

    void CommitTransaction(const TTransactionId& transactionId)
    {
        auto transactionSupervisor = Bootstrap_->GetTransactionSupervisor();
        WaitFor(transactionSupervisor->CommitTransaction(transactionId))
            .ThrowOnError();
    }

    void CreateNode(
        const TYPath& path,
        const TTransactionId& transactionId,
        EObjectType type,
        const TYsonString& attributes = TYsonString("{}"))
    {
        auto service = Bootstrap_->GetObjectManager()->GetRootService();
        auto req = TCypressYPathProxy::Create(path);
        SetTransactionId(req, transactionId);
        req->set_type(static_cast<int>(type));
        req->set_recursive(true);
        ToProto(req->mutable_node_attributes(), *ConvertToAttributes(attributes));
        WaitFor(ExecuteVerb(service, req))
            .ThrowOnError();
    }

};

////////////////////////////////////////////////////////////////////////////////

TWorldInitializer::TWorldInitializer(
    TCellMasterConfigPtr config,
    TBootstrap* bootstrap)
    : Impl_(New<TImpl>(config, bootstrap))
{ }

TWorldInitializer::~TWorldInitializer()
{ }

bool TWorldInitializer::CheckInitialized()
{
    return Impl_->CheckInitialized();
}

bool TWorldInitializer::CheckProvisionLock()
{
    return Impl_->CheckProvisionLock();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellMaster
} // namespace NYT

