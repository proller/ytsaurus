#include "table_replica_proxy.h"
#include "table_replica.h"
#include "tablet_manager.h"

#include <yt/yt/server/master/object_server/object_detail.h>

#include <yt/yt/server/lib/misc/interned_attributes.h>

#include <yt/yt/server/master/cypress_server/cypress_manager.h>

#include <yt/yt/server/master/chunk_server/chunk_list.h>

#include <yt/yt/server/master/table_server/replicated_table_node.h>

#include <yt/yt/server/master/cell_master/bootstrap.h>

#include <yt/yt/client/transaction_client/timestamp_provider.h>

#include <yt/yt/ytlib/tablet_client/proto/table_replica_ypath.pb.h>

#include <yt/yt/core/ytree/fluent.h>

namespace NYT::NTabletServer {

using namespace NYTree;
using namespace NYson;
using namespace NTabletClient;
using namespace NObjectClient;
using namespace NObjectServer;
using namespace NCypressServer;
using namespace NTransactionClient;

////////////////////////////////////////////////////////////////////////////////

class TTableReplicaProxy
    : public TNonversionedObjectProxyBase<TTableReplica>
{
public:
    TTableReplicaProxy(
        NCellMaster::TBootstrap* bootstrap,
        TObjectTypeMetadata* metadata,
        TTableReplica* replica)
        : TBase(bootstrap, metadata, replica)
    { }

private:
    typedef TNonversionedObjectProxyBase<TTableReplica> TBase;

    virtual void ValidateRemoval() override
    {
        auto* replica = GetThisImpl();
        auto* table = replica->GetTable();

        const auto& cypressManager = Bootstrap_->GetCypressManager();
        cypressManager->LockNode(table, nullptr, TLockRequest(ELockMode::Exclusive));
    }

    virtual void ListSystemAttributes(std::vector<TAttributeDescriptor>* attributes) override
    {
        attributes->push_back(EInternedAttributeKey::ClusterName);
        attributes->push_back(EInternedAttributeKey::ReplicaPath);
        attributes->push_back(EInternedAttributeKey::TableId);
        attributes->push_back(TAttributeDescriptor(EInternedAttributeKey::TablePath)
            .SetOpaque(true));
        attributes->push_back(EInternedAttributeKey::StartReplicationTimestamp);
        attributes->push_back(EInternedAttributeKey::State);
        attributes->push_back(EInternedAttributeKey::Mode);
        attributes->push_back(TAttributeDescriptor(EInternedAttributeKey::Tablets)
            .SetOpaque(true));
        attributes->push_back(TAttributeDescriptor(EInternedAttributeKey::ErrorCount)
            .SetOpaque(true));
        attributes->push_back(TAttributeDescriptor(EInternedAttributeKey::ReplicationLagTime)
            .SetOpaque(true));
        attributes->push_back(TAttributeDescriptor(EInternedAttributeKey::EnableReplicatedTableTracker)
            .SetWritable(true));
        attributes->push_back(EInternedAttributeKey::PreserveTimestamps);
        attributes->push_back(EInternedAttributeKey::Atomicity);

        TBase::ListSystemAttributes(attributes);
    }

    virtual bool GetBuiltinAttribute(TInternedAttributeKey key, IYsonConsumer* consumer) override
    {
        auto* replica = GetThisImpl();
        auto* table = replica->GetTable();
        const auto& timestampProvider = Bootstrap_->GetTimestampProvider();
        const auto& cypressManager = Bootstrap_->GetCypressManager();

        switch (key) {
            case EInternedAttributeKey::ClusterName:
                BuildYsonFluently(consumer)
                    .Value(replica->GetClusterName());
                return true;

            case EInternedAttributeKey::ReplicaPath:
                BuildYsonFluently(consumer)
                    .Value(replica->GetReplicaPath());
                return true;

            case EInternedAttributeKey::StartReplicationTimestamp:
                BuildYsonFluently(consumer)
                    .Value(replica->GetStartReplicationTimestamp());
                return true;

            case EInternedAttributeKey::TableId: {
                BuildYsonFluently(consumer)
                    .Value(table->GetId());
                return true;
            }

            case EInternedAttributeKey::TablePath: {
                if (table->IsForeign()) {
                    break;
                }
                BuildYsonFluently(consumer)
                    .Value(cypressManager->GetNodePath(
                        table->GetTrunkNode(),
                        nullptr));
                return true;
            }

            case EInternedAttributeKey::State:
                BuildYsonFluently(consumer)
                    .Value(replica->GetState());
                return true;

            case EInternedAttributeKey::Mode:
                BuildYsonFluently(consumer)
                    .Value(replica->GetMode());
                return true;

            case EInternedAttributeKey::Tablets:
                BuildYsonFluently(consumer)
                    .DoListFor(table->Tablets(), [=] (TFluentList fluent, TTablet* tablet) {
                        const auto* chunkList = tablet->GetChunkList();
                        const auto* replicaInfo = tablet->GetReplicaInfo(replica);
                        fluent
                            .Item().BeginMap()
                                .Item("tablet_id").Value(tablet->GetId())
                                .Item("state").Value(replicaInfo->GetState())
                                .Item("current_replication_row_index").Value(replicaInfo->GetCurrentReplicationRowIndex())
                                .Item("current_replication_timestamp").Value(replicaInfo->GetCurrentReplicationTimestamp())
                                .Item("replication_lag_time").Value(tablet->ComputeReplicationLagTime(
                                    timestampProvider->GetLatestTimestamp(), *replicaInfo))
                                .Item("has_error").Value(replicaInfo->GetHasError())
                                .Item("trimmed_row_count").Value(tablet->GetTrimmedRowCount())
                                .Item("flushed_row_count").Value(chunkList->Statistics().LogicalRowCount)
                            .EndMap();
                    });
                return true;

            case EInternedAttributeKey::ErrorCount:
                BuildYsonFluently(consumer)
                    .Value(replica->GetErrorCount());
                return true;

            case EInternedAttributeKey::ReplicationLagTime:
                BuildYsonFluently(consumer)
                    .Value(replica->ComputeReplicationLagTime(timestampProvider->GetLatestTimestamp()));
                return true;

            case EInternedAttributeKey::EnableReplicatedTableTracker:
                BuildYsonFluently(consumer)
                    .Value(replica->GetEnableReplicatedTableTracker());
                return true;

            case EInternedAttributeKey::PreserveTimestamps:
                BuildYsonFluently(consumer)
                    .Value(replica->GetPreserveTimestamps());
                return true;

            case EInternedAttributeKey::Atomicity:
                BuildYsonFluently(consumer)
                    .Value(replica->GetAtomicity());
                return true;

            default:
                break;
        }

        return TBase::GetBuiltinAttribute(key, consumer);
    }

    virtual TFuture<NYson::TYsonString> GetBuiltinAttributeAsync(NYTree::TInternedAttributeKey key) override
    {
        auto* replica = GetThisImpl();
        auto* table = replica->GetTable();

        switch (key) {
            case EInternedAttributeKey::TablePath:
                return FetchFromShepherd(FromObjectId(table->GetId()) + "/@path");

            default:
                break;
        }

        return TBase::GetBuiltinAttributeAsync(key);
    }

    bool SetBuiltinAttribute(TInternedAttributeKey key, const TYsonString& value) override
    {
        auto* replica = GetThisImpl();

        switch (key) {
            case EInternedAttributeKey::EnableReplicatedTableTracker: {
                ValidateNoTransaction();

                replica->SetEnableReplicatedTableTracker(ConvertTo<bool>(value));
                return true;
            }
        }

        return TBase::SetBuiltinAttribute(key, value);
    }

    virtual bool DoInvoke(const NRpc::IServiceContextPtr& context) override
    {
        DISPATCH_YPATH_SERVICE_METHOD(Alter);
        return TBase::DoInvoke(context);
    }

    DECLARE_YPATH_SERVICE_METHOD(NTabletClient::NProto, Alter)
    {
        Y_UNUSED(response);

        DeclareMutating();

        auto enabled = request->has_enabled() ? std::make_optional(request->enabled()) : std::nullopt;
        auto mode = request->has_mode() ? std::make_optional(ETableReplicaMode(request->mode())) : std::nullopt;
        auto atomicity = request->has_atomicity() ? std::make_optional(NTransactionClient::EAtomicity(request->atomicity())) : std::nullopt;
        auto preserveTimestamps = request->has_preserve_timestamps() ? std::make_optional(request->preserve_timestamps()) : std::nullopt;

        context->SetRequestInfo("Enabled: %v, Mode: %v",
            enabled,
            mode);

        auto* replica = GetThisImpl();

        const auto& tabletManager = Bootstrap_->GetTabletManager();
        if (enabled || mode || atomicity || preserveTimestamps) {
            tabletManager->AlterTableReplica(
                replica,
                std::move(enabled),
                std::move(mode),
                std::move(atomicity),
                std::move(preserveTimestamps));
        }

        context->Reply();
    }
};

IObjectProxyPtr CreateTableReplicaProxy(
    NCellMaster::TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TTableReplica* replica)
{
    return New<TTableReplicaProxy>(bootstrap, metadata, replica);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletServer

