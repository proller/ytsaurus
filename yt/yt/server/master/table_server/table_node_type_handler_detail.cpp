#include "table_node_type_handler_detail.h"
#include "table_node.h"
#include "table_node_proxy.h"
#include "replicated_table_node.h"
#include "replicated_table_node_proxy.h"
#include "shared_table_schema.h"
#include "private.h"

#include <yt/yt/ytlib/table_client/schema.h>

#include <yt/yt/ytlib/chunk_client/helpers.h>

#include <yt/yt/server/master/cell_master/bootstrap.h>
#include <yt/yt/server/master/cell_master/config.h>
#include <yt/yt/server/master/cell_master/config_manager.h>

#include <yt/yt/server/master/chunk_server/chunk.h>
#include <yt/yt/server/master/chunk_server/chunk_list.h>
#include <yt/yt/server/master/chunk_server/chunk_manager.h>
#include <yt/yt/server/master/chunk_server/chunk_owner_type_handler.h>

#include <yt/yt/server/master/tablet_server/tablet.h>
#include <yt/yt/server/master/tablet_server/tablet_manager.h>

#include <yt/yt/server/master/table_server/shared_table_schema.h>

namespace NYT::NTableServer {

using namespace NTableClient;
using namespace NCellMaster;
using namespace NCypressServer;
using namespace NYTree;
using namespace NYson;
using namespace NChunkServer;
using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NObjectClient;
using namespace NObjectServer;
using namespace NTransactionServer;
using namespace NSecurityServer;
using namespace NTabletServer;

////////////////////////////////////////////////////////////////////////////////

template <class TImpl>
bool TTableNodeTypeHandlerBase<TImpl>::HasBranchedChangesImpl(
    TImpl* originatingNode,
    TImpl* branchedNode)
{
    if (TBase::HasBranchedChangesImpl(originatingNode, branchedNode))  {
        return true;
    }

    if (branchedNode->IsDynamic()) {
        YT_VERIFY(originatingNode->IsDynamic());
        // One may consider supporting unlocking unmounted dynamic tables.
        // However, it isn't immediately obvious why that should be useful and
        // allowing to unlock something always requires careful consideration.
        return true;
    }

    return false;
}

template <class TImpl>
std::unique_ptr<TImpl> TTableNodeTypeHandlerBase<TImpl>::DoCreate(
    const TVersionedNodeId& id,
    const TCreateNodeContext& context)
{
    const auto& dynamicConfig = this->Bootstrap_->GetConfigManager()->GetConfig();
    const auto& cypressManagerConfig = this->Bootstrap_->GetConfig()->CypressManager;
    const auto& chunkManagerConfig = this->Bootstrap_->GetConfigManager()->GetConfig()->ChunkManager;

    if (auto compressionCodecValue = context.ExplicitAttributes->FindYson("compression_codec")) {
        ValidateCompressionCodec(
            compressionCodecValue,
            chunkManagerConfig->DeprecatedCodecIds,
            chunkManagerConfig->DeprecatedCodecNameToAlias);
    }

    auto combinedAttributes = OverlayAttributeDictionaries(context.ExplicitAttributes, context.InheritedAttributes);
    auto optionalTabletCellBundleName = combinedAttributes->FindAndRemove<TString>("tablet_cell_bundle");
    auto optimizeFor = combinedAttributes->GetAndRemove<EOptimizeFor>("optimize_for", EOptimizeFor::Lookup);
    auto replicationFactor = combinedAttributes->GetAndRemove("replication_factor", cypressManagerConfig->DefaultTableReplicationFactor);
    auto compressionCodec = combinedAttributes->GetAndRemove<NCompression::ECodec>("compression_codec", NCompression::ECodec::Lz4);
    auto erasureCodec = combinedAttributes->GetAndRemove<NErasure::ECodec>("erasure_codec", NErasure::ECodec::None);

    ValidateReplicationFactor(replicationFactor);

    bool dynamic = combinedAttributes->GetAndRemove<bool>("dynamic", false);
    bool replicated = TypeFromId(id.ObjectId) == EObjectType::ReplicatedTable;

    if (replicated && !dynamic) {
        THROW_ERROR_EXCEPTION("Replicated table must be dynamic");
    }

    auto schema = combinedAttributes->FindAndRemove<TTableSchemaPtr>("schema");

    if (dynamic && !schema) {
        THROW_ERROR_EXCEPTION("\"schema\" is mandatory for dynamic tables");
    }

    if (replicated) {
        if (!dynamic) {
            THROW_ERROR_EXCEPTION("Replicated table must be dynamic");
        }
    }

    if (schema) {
        // NB: Sorted dynamic tables contain unique keys, set this for user.
        if (dynamic && schema->IsSorted() && !schema->GetUniqueKeys()) {
            schema = schema->ToUniqueKeys();
        }

        if (schema->HasNontrivialSchemaModification()) {
            THROW_ERROR_EXCEPTION("Cannot create table with nontrivial schema modification");
        }

        ValidateTableSchemaUpdate(TTableSchema(), *schema, dynamic, true);

        if (!dynamicConfig->EnableDescendingSortOrder || (dynamic && !dynamicConfig->EnableDescendingSortOrderDynamic)) {
            ValidateNoDescendingSortOrder(*schema);
        }
    }

    auto optionalTabletCount = combinedAttributes->FindAndRemove<int>("tablet_count");
    auto optionalPivotKeys = combinedAttributes->FindAndRemove<std::vector<TLegacyOwningKey>>("pivot_keys");
    if (optionalTabletCount && optionalPivotKeys) {
        THROW_ERROR_EXCEPTION("Cannot specify both \"tablet_count\" and \"pivot_keys\"");
    }
    auto upstreamReplicaId = combinedAttributes->GetAndRemove<TTableReplicaId>("upstream_replica_id", TTableReplicaId());
    if (upstreamReplicaId) {
        if (!dynamic) {
            THROW_ERROR_EXCEPTION("Upstream replica can only be set for dynamic tables");
        }
        if (replicated) {
            THROW_ERROR_EXCEPTION("Upstream replica cannot be set for replicated tables");
        }
    }

    const auto& tabletManager = this->Bootstrap_->GetTabletManager();
    auto* tabletCellBundle = optionalTabletCellBundleName
        ? tabletManager->GetTabletCellBundleByNameOrThrow(*optionalTabletCellBundleName, true /*activeLifeStageOnly*/)
        : tabletManager->GetDefaultTabletCellBundle();

    auto nodeHolder = this->DoCreateImpl(
        id,
        context,
        replicationFactor,
        compressionCodec,
        erasureCodec);
    auto* node = nodeHolder.get();

    try {
        node->SetOptimizeFor(optimizeFor);

        if (node->IsReplicated()) {
            // NB: This setting is not visible in attributes but crucial for replication
            // to work properly.
            node->SetCommitOrdering(NTransactionClient::ECommitOrdering::Strong);
        }

        if (schema) {
            const auto& registry = this->Bootstrap_->GetCypressManager()->GetSharedTableSchemaRegistry();
            node->SharedTableSchema() = registry->GetSchema(std::move(*schema));
            node->SetSchemaMode(ETableSchemaMode::Strong);
        }

        // NB: Dynamic table should have a bundle during creation for accounting to work properly.
        tabletManager->SetTabletCellBundle(node, tabletCellBundle);

        if (dynamic) {
            if (node->IsNative()) {
                tabletManager->ValidateMakeTableDynamic(node);
            }

            tabletManager->MakeTableDynamic(node);

            if (node->IsNative()) {
                if (optionalTabletCount) {
                    tabletManager->PrepareReshardTable(node, 0, 0, *optionalTabletCount, {}, true);
                } else if (optionalPivotKeys) {
                    tabletManager->PrepareReshardTable(node, 0, 0, optionalPivotKeys->size(), *optionalPivotKeys, true);
                }
            }

            if (!node->IsExternal()) {
                if (optionalTabletCount) {
                    tabletManager->ReshardTable(node, 0, 0, *optionalTabletCount, {});
                } else if (optionalPivotKeys) {
                    tabletManager->ReshardTable(node, 0, 0, optionalPivotKeys->size(), *optionalPivotKeys);
                }
            }

            node->SetUpstreamReplicaId(upstreamReplicaId);
        }
    } catch (const std::exception&) {
        DoDestroy(node);
        throw;
    }

    return nodeHolder;
}

template <class TImpl>
void TTableNodeTypeHandlerBase<TImpl>::DoDestroy(TImpl* table)
{
    TBase::DoDestroy(table);

    if (table->IsTrunk()) {
        const auto& tabletManager = this->Bootstrap_->GetTabletManager();
        tabletManager->DestroyTable(table);
    }
}

template <class TImpl>
void TTableNodeTypeHandlerBase<TImpl>::DoBranch(
    const TImpl* originatingNode,
    TImpl* branchedNode,
    const TLockRequest& lockRequest)
{
    branchedNode->SharedTableSchema() = originatingNode->SharedTableSchema();
    branchedNode->SetSchemaMode(originatingNode->GetSchemaMode());
    branchedNode->SetOptimizeFor(originatingNode->GetOptimizeFor());
    branchedNode->SetProfilingMode(originatingNode->GetProfilingMode());
    branchedNode->SetProfilingTag(originatingNode->GetProfilingTag());

    // Save current retained and unflushed timestamps in locked node.
    branchedNode->SetRetainedTimestamp(originatingNode->GetCurrentRetainedTimestamp());
    branchedNode->SetUnflushedTimestamp(originatingNode->GetCurrentUnflushedTimestamp(lockRequest.Timestamp));

    TBase::DoBranch(originatingNode, branchedNode, lockRequest);
}

template <class TImpl>
void TTableNodeTypeHandlerBase<TImpl>::DoMerge(
    TImpl* originatingNode,
    TImpl* branchedNode)
{
    originatingNode->SharedTableSchema() = branchedNode->SharedTableSchema();
    originatingNode->SetSchemaMode(branchedNode->GetSchemaMode());
    originatingNode->MergeOptimizeFor(branchedNode);
    originatingNode->SetProfilingMode(branchedNode->GetProfilingMode());
    originatingNode->SetProfilingTag(branchedNode->GetProfilingTag());

    TBase::DoMerge(originatingNode, branchedNode);
}

template <class TImpl>
void TTableNodeTypeHandlerBase<TImpl>::DoClone(
    TImpl* sourceNode,
    TImpl* clonedTrunkNode,
    ICypressNodeFactory* factory,
    ENodeCloneMode mode,
    TAccount* account)
{
    const auto& tabletManager = this->Bootstrap_->GetTabletManager();
    tabletManager->ValidateCloneTable(
        sourceNode,
        mode,
        account);

    TBase::DoClone(sourceNode, clonedTrunkNode, factory, mode, account);

    // NB: Dynamic table should have a bundle during creation for accounting to work properly.
    auto* trunkSourceNode = sourceNode->GetTrunkNode();
    tabletManager->SetTabletCellBundle(clonedTrunkNode, trunkSourceNode->GetTabletCellBundle());

    if (sourceNode->IsDynamic()) {
        tabletManager->CloneTable(
            sourceNode,
            clonedTrunkNode,
            mode);
    }

    clonedTrunkNode->SharedTableSchema() = sourceNode->SharedTableSchema();
    clonedTrunkNode->SetSchemaMode(sourceNode->GetSchemaMode());
    clonedTrunkNode->SetOptimizeFor(sourceNode->GetOptimizeFor());

    if (trunkSourceNode->HasCustomDynamicTableAttributes()) {
        clonedTrunkNode->SetDynamic(trunkSourceNode->IsDynamic());
        clonedTrunkNode->SetAtomicity(trunkSourceNode->GetAtomicity());
        clonedTrunkNode->SetCommitOrdering(trunkSourceNode->GetCommitOrdering());
        clonedTrunkNode->SetInMemoryMode(trunkSourceNode->GetInMemoryMode());
        clonedTrunkNode->SetUpstreamReplicaId(trunkSourceNode->GetUpstreamReplicaId());
        clonedTrunkNode->SetLastCommitTimestamp(trunkSourceNode->GetLastCommitTimestamp());
        clonedTrunkNode->MutableTabletBalancerConfig() = CloneYsonSerializable(trunkSourceNode->TabletBalancerConfig());
        clonedTrunkNode->SetEnableDynamicStoreRead(trunkSourceNode->GetEnableDynamicStoreRead());
        clonedTrunkNode->SetProfilingMode(trunkSourceNode->GetProfilingMode());
        clonedTrunkNode->SetProfilingTag(trunkSourceNode->GetProfilingTag());
    }
}

template <class TImpl>
void TTableNodeTypeHandlerBase<TImpl>::DoBeginCopy(
    TImpl* node,
    TBeginCopyContext* context)
{
    TBase::DoBeginCopy(node, context);

    const auto& tabletManager = this->Bootstrap_->GetTabletManager();
    tabletManager->ValidateBeginCopyTable(node, context->GetMode());

    // TODO(babenko): support copying dynamic tables
    if (node->IsDynamic()) {
        THROW_ERROR_EXCEPTION("Dynamic tables do not support cross-cell copying");
    }

    using NYT::Save;
    auto* trunkNode = node->GetTrunkNode();
    Save(*context, trunkNode->GetTabletCellBundle());

    const auto& schema = node->SharedTableSchema();
    Save(*context, schema.operator bool());
    if (schema) {
        Save(*context, context->GetTableSchemaRegistry()->Intern(schema->GetTableSchema()));
    }

    Save(*context, node->GetSchemaMode());
    Save(*context, node->GetOptimizeFor());

    Save(*context, trunkNode->HasCustomDynamicTableAttributes());
    if (trunkNode->HasCustomDynamicTableAttributes()) {
        Save(*context, trunkNode->IsDynamic());
        Save(*context, trunkNode->GetAtomicity());
        Save(*context, trunkNode->GetCommitOrdering());
        Save(*context, trunkNode->GetInMemoryMode());
        Save(*context, trunkNode->GetUpstreamReplicaId());
        Save(*context, trunkNode->GetLastCommitTimestamp());
        Save(*context, ConvertToYsonString(trunkNode->TabletBalancerConfig()));
        Save(*context, trunkNode->GetEnableDynamicStoreRead());
        Save(*context, trunkNode->GetProfilingMode());
        Save(*context, trunkNode->GetProfilingTag());
    }
}

template <class TImpl>
void TTableNodeTypeHandlerBase<TImpl>::DoEndCopy(
    TImpl* node,
    TEndCopyContext* context,
    ICypressNodeFactory* factory)
{
    TBase::DoEndCopy(node, context, factory);

    const auto& tabletManager = this->Bootstrap_->GetTabletManager();
    // TODO(babenko): support copying dynamic tables

    using NYT::Load;

    auto* bundle = Load<TTabletCellBundle*>(*context);
    if (bundle) {
        const auto& objectManager = this->Bootstrap_->GetObjectManager();
        objectManager->ValidateObjectLifeStage(bundle);
        tabletManager->SetTabletCellBundle(node, bundle);
    }

    if (Load<bool>(*context)) {
        auto schema = Load<TInternedTableSchema>(*context);
        const auto& registry = this->Bootstrap_->GetCypressManager()->GetSharedTableSchemaRegistry();
        node->SharedTableSchema() = registry->GetSchema(*schema);
    }

    node->SetSchemaMode(Load<ETableSchemaMode>(*context));
    node->SetOptimizeFor(Load<EOptimizeFor>(*context));

    if (Load<bool>(*context)) {
        node->SetDynamic(Load<bool>(*context));
        node->SetAtomicity(Load<NTransactionClient::EAtomicity>(*context));
        node->SetCommitOrdering(Load<NTransactionClient::ECommitOrdering>(*context));
        node->SetInMemoryMode(Load<NTabletClient::EInMemoryMode>(*context));
        node->SetUpstreamReplicaId(Load<TTableReplicaId>(*context));
        node->SetLastCommitTimestamp(Load<TTimestamp>(*context));
        node->MutableTabletBalancerConfig() = ConvertTo<TTabletBalancerConfigPtr>(Load<TYsonString>(*context));
        node->SetEnableDynamicStoreRead(Load<std::optional<bool>>(*context));
        node->SetProfilingMode(Load<NTabletNode::EDynamicTableProfilingMode>(*context));
        node->SetProfilingTag(Load<TString>(*context));
    }
}

template<class TImpl>
bool TTableNodeTypeHandlerBase<TImpl>::IsSupportedInheritableAttribute(const TString& key) const
{
    static const THashSet<TString> SupportedInheritableAttributes{
        "atomicity",
        "commit_ordering",
        "in_memory_mode",
        "optimize_for",
        "tablet_cell_bundle",
        "profiling_mode",
        "profiling_tag"
    };

    if (SupportedInheritableAttributes.contains(key)) {
        return true;
    }

    return TBase::IsSupportedInheritableAttribute(key);
}

template <class TImpl>
std::optional<std::vector<TString>> TTableNodeTypeHandlerBase<TImpl>::DoListColumns(TImpl* node) const
{
    const auto& sharedSchema = node->SharedTableSchema();
    if (!sharedSchema) {
        return std::nullopt;
    }

    const auto& schema = sharedSchema->GetTableSchema();
    std::vector<TString> result;
    result.reserve(schema.Columns().size());
    for (const auto& column : schema.Columns()) {
        result.push_back(column.Name());
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

EObjectType TTableNodeTypeHandler::GetObjectType() const
{
    return EObjectType::Table;
}

ICypressNodeProxyPtr TTableNodeTypeHandler::DoGetProxy(
    TTableNode* trunkNode,
    TTransaction* transaction)
{
    return CreateTableNodeProxy(
        Bootstrap_,
        &Metadata_,
        transaction,
        trunkNode);
}

////////////////////////////////////////////////////////////////////////////////

EObjectType TReplicatedTableNodeTypeHandler::GetObjectType() const
{
    return EObjectType::ReplicatedTable;
}

bool TReplicatedTableNodeTypeHandler::HasBranchedChangesImpl(
    TReplicatedTableNode* originatingNode,
    TReplicatedTableNode* branchedNode)
{
    // Forbid explicitly unlocking replicated tables.
    return true;
}

ICypressNodeProxyPtr TReplicatedTableNodeTypeHandler::DoGetProxy(
    TReplicatedTableNode* trunkNode,
    TTransaction* transaction)
{
    return CreateReplicatedTableNodeProxy(
        Bootstrap_,
        &Metadata_,
        transaction,
        trunkNode);
}

void TReplicatedTableNodeTypeHandler::DoBeginCopy(
    TReplicatedTableNode* node,
    TBeginCopyContext* context)
{
    // TODO(babenko): support cross-cell copy for replicated tables
    THROW_ERROR_EXCEPTION("Replicated tables do not support cross-cell copying");
}

void TReplicatedTableNodeTypeHandler::DoEndCopy(
    TReplicatedTableNode* node,
    TEndCopyContext* context,
    ICypressNodeFactory* factory)
{
    // TODO(babenko): support cross-cell copy for replicated tables
    THROW_ERROR_EXCEPTION("Replicated tables do not support cross-cell copying");
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableServer

