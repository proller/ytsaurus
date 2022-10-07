#include "chunk_owner_type_handler.h"
#include "chunk_manager.h"
#include "medium.h"
#include "helpers.h"

#include <yt/yt/server/master/cypress_server/node_detail.h>

#include <yt/yt/server/master/cypress_server/node.h>
#include <yt/yt/server/master/cypress_server/cypress_manager.h>

#include <yt/yt/server/master/chunk_server/chunk_manager.h>

#include <yt/yt/server/master/object_server/object_manager.h>

#include <yt/yt/server/master/security_server/security_manager.h>
#include <yt/yt/server/master/security_server/security_tags.h>

#include <yt/yt/server/master/table_server/table_manager.h>

#include <yt/yt/server/master/tablet_server/hunk_storage_node.h>
#include <yt/yt/server/master/tablet_server/tablet_manager.h>

#include <yt/yt/server/master/cell_master/hydra_facade.h>
#include <yt/yt/server/master/cell_master/config.h>

#include <yt/yt/server/master/file_server/file_node.h>

#include <yt/yt/server/master/table_server/table_node.h>
#include <yt/yt/server/master/table_server/replicated_table_node.h>

#include <yt/yt/server/master/journal_server/journal_node.h>

#include <yt/yt/client/chunk_client/data_statistics.h>

#include <yt/yt/ytlib/chunk_client/helpers.h>

#include <yt/yt/core/ytree/interned_attributes.h>

namespace NYT::NChunkServer {

using namespace NYTree;
using namespace NFileServer;
using namespace NTableServer;
using namespace NJournalServer;
using namespace NCypressServer;
using namespace NSecurityServer;
using namespace NObjectServer;
using namespace NChunkClient;

////////////////////////////////////////////////////////////////////////////////

template <class TChunkOwner>
TChunkOwnerTypeHandler<TChunkOwner>::TChunkOwnerTypeHandler(NCellMaster::TBootstrap* bootstrap)
    : TBase(bootstrap)
    , Logger(ChunkServerLogger)
{ }

template <class TChunkOwner>
ETypeFlags TChunkOwnerTypeHandler<TChunkOwner>::GetFlags() const
{
    return TBase::GetFlags() | ETypeFlags::Externalizable;
}

template <class TChunkOwner>
ENodeType TChunkOwnerTypeHandler<TChunkOwner>::GetNodeType() const
{
    return ENodeType::Entity;
}

template <class TChunkOwner>
bool TChunkOwnerTypeHandler<TChunkOwner>::IsSupportedInheritableAttribute(const TString& key) const
{
    static const THashSet<TString> SupportedInheritableAttributes{
        "compression_codec",
        "erasure_codec",
        "media"
        "primary_medium",
        "replication_factor",
        "vital",
        "enable_chunk_merger"
    };

    return SupportedInheritableAttributes.contains(key);
}

template <class TChunkOwner>
bool TChunkOwnerTypeHandler<TChunkOwner>::HasBranchedChangesImpl(TChunkOwner* originatingNode, TChunkOwner* branchedNode)
{
    if (TBase::HasBranchedChangesImpl(originatingNode, branchedNode)) {
        return true;
    }

    return
        branchedNode->GetUpdateMode() != NChunkClient::EUpdateMode::None ||
        branchedNode->GetPrimaryMediumIndex() != originatingNode->GetPrimaryMediumIndex() ||
        branchedNode->Replication() != originatingNode->Replication() ||
        branchedNode->GetCompressionCodec() != originatingNode->GetCompressionCodec() ||
        branchedNode->GetErasureCodec() != originatingNode->GetErasureCodec() ||
        branchedNode->GetEnableStripedErasure() != originatingNode->GetEnableStripedErasure() ||
        branchedNode->GetEnableSkynetSharing() != originatingNode->GetEnableSkynetSharing() ||
        !branchedNode->DeltaSecurityTags()->IsEmpty() ||
        !TInternedSecurityTags::RefEqual(branchedNode->SnapshotSecurityTags(), originatingNode->SnapshotSecurityTags());
}

template <class TChunkOwner>
std::unique_ptr<TChunkOwner> TChunkOwnerTypeHandler<TChunkOwner>::DoCreateImpl(
    TVersionedNodeId id,
    const TCreateNodeContext& context,
    int replicationFactor,
    NCompression::ECodec compressionCodec,
    NErasure::ECodec erasureCodec,
    bool enableStripedErasure,
    EChunkListKind rootChunkListKind)
{
    const auto& chunkManager = this->Bootstrap_->GetChunkManager();

    auto combinedAttributes = OverlayAttributeDictionaries(context.ExplicitAttributes, context.InheritedAttributes);

    auto primaryMediumName = combinedAttributes->GetAndRemove<TString>("primary_medium", NChunkClient::DefaultStoreMediumName);
    auto* primaryMedium = chunkManager->GetMediumByNameOrThrow(primaryMediumName);

    std::optional<TSecurityTags> securityTags;
    if (auto securityTagItems = combinedAttributes->FindAndRemove<TSecurityTagsItems>("security_tags")) {
        securityTags = TSecurityTags{std::move(*securityTagItems)};
        securityTags->Validate();
    }

    auto nodeHolder = TBase::DoCreate(id, context);
    auto* node = nodeHolder.get();

    auto chunkMergerMode = combinedAttributes->GetAndRemove<EChunkMergerMode>("chunk_merger_mode", EChunkMergerMode::None);

    try {
        node->SetPrimaryMediumIndex(primaryMedium->GetIndex());

        node->Replication().Set(primaryMedium->GetIndex(), TReplicationPolicy(replicationFactor, false));

        node->SetCompressionCodec(compressionCodec);
        node->SetErasureCodec(erasureCodec);
        node->SetEnableStripedErasure(enableStripedErasure);

        node->SetChunkMergerMode(chunkMergerMode);

        if (securityTags) {
            const auto& securityManager = this->Bootstrap_->GetSecurityManager();
            const auto& securityTagsRegistry = securityManager->GetSecurityTagsRegistry();
            node->SnapshotSecurityTags() = securityTagsRegistry->Intern(std::move(*securityTags));
        }

        if (!node->IsExternal()) {
            // Create an empty chunk list and reference it from the node.
            auto* chunkList = chunkManager->CreateChunkList(rootChunkListKind);
            node->SetChunkList(chunkList);
            chunkList->AddOwningNode(node);
        }
    } catch (const std::exception&) {
        this->Destroy(node);
        throw;
    }

    return nodeHolder;
}

template <class TChunkOwner>
void TChunkOwnerTypeHandler<TChunkOwner>::DoDestroy(TChunkOwner* node)
{
    for (auto* chunkList : node->GetChunkLists()) {
        if (chunkList) {
            if (node->IsTrunk() && !node->IsExternal()) {
                const auto& chunkManager = TBase::Bootstrap_->GetChunkManager();
                chunkManager->ScheduleChunkRequisitionUpdate(chunkList);
            }

            chunkList->RemoveOwningNode(node);
        }
    }

    TBase::DoDestroy(node);
}

template <class TChunkOwner>
void TChunkOwnerTypeHandler<TChunkOwner>::DoBranch(
    const TChunkOwner* originatingNode,
    TChunkOwner* branchedNode,
    const TLockRequest& lockRequest)
{
    TBase::DoBranch(originatingNode, branchedNode, lockRequest);

    if (!originatingNode->IsExternal()) {
        for (auto contentType : TEnumTraits<EChunkListContentType>::GetDomainValues()) {
            auto* chunkList = originatingNode->GetChunkList(contentType);
            branchedNode->SetChunkList(contentType, chunkList);
            if (chunkList) {
                chunkList->AddOwningNode(branchedNode);
            }
        }
    }

    branchedNode->SetPrimaryMediumIndex(originatingNode->GetPrimaryMediumIndex());
    branchedNode->Replication() = originatingNode->Replication();
    branchedNode->SnapshotStatistics() = originatingNode->ComputeTotalStatistics();

    if (originatingNode->DeltaSecurityTags()->IsEmpty()) {
        // Fast path.
        branchedNode->SnapshotSecurityTags() = originatingNode->SnapshotSecurityTags();
    } else {
        // Slow path.
        const auto& securityManager = TBase::Bootstrap_->GetSecurityManager();
        const auto& securityTagsRegistry = securityManager->GetSecurityTagsRegistry();
        branchedNode->SnapshotSecurityTags() = securityTagsRegistry->Intern(originatingNode->GetSecurityTags());
    }
}

template <class TChunkOwner>
void TChunkOwnerTypeHandler<TChunkOwner>::DoLogBranch(
    const TChunkOwner* originatingNode,
    TChunkOwner* branchedNode,
    const TLockRequest& lockRequest)
{
    const auto& chunkManager = TBase::Bootstrap_->GetChunkManager();
    const auto* primaryMedium = chunkManager->GetMediumByIndex(originatingNode->GetPrimaryMediumIndex());
    YT_LOG_DEBUG_IF(
        TBase::IsMutationLoggingEnabled(),
        "Node branched (OriginatingNodeId: %v, BranchedNodeId: %v, ChunkListId: %v, HunkChunkListId: %v"
        "PrimaryMedium: %v, Replication: %v, Mode: %v, LockTimestamp: %x)",
        originatingNode->GetVersionedId(),
        branchedNode->GetVersionedId(),
        NObjectServer::GetObjectId(originatingNode->GetChunkList()),
        NObjectServer::GetObjectId(originatingNode->GetHunkChunkList()),
        primaryMedium->GetName(),
        originatingNode->Replication(),
        lockRequest.Mode,
        lockRequest.Timestamp);
}

template <class TChunkOwner>
void TChunkOwnerTypeHandler<TChunkOwner>::DoMerge(
    TChunkOwner* originatingNode,
    TChunkOwner* branchedNode)
{
    TBase::DoMerge(originatingNode, branchedNode);

    // Merge builtin attributes.
    originatingNode->MergeCompressionCodec(branchedNode);
    originatingNode->MergeErasureCodec(branchedNode);
    originatingNode->MergeEnableStripedErasure(branchedNode);
    originatingNode->MergeEnableSkynetSharing(branchedNode);

    bool isExternal = originatingNode->IsExternal();

    const auto& chunkManager = TBase::Bootstrap_->GetChunkManager();
    const auto& securityManager = TBase::Bootstrap_->GetSecurityManager();
    const auto& securityTagsRegistry = securityManager->GetSecurityTagsRegistry();

    auto* originatingChunkList = originatingNode->GetChunkList();
    auto* branchedChunkList = branchedNode->GetChunkList();

    auto originatingMode = originatingNode->GetUpdateMode();
    auto branchedMode = branchedNode->GetUpdateMode();

    if (!isExternal) {
        for (auto* branchedChunkList : branchedNode->GetChunkLists()) {
            if (branchedChunkList) {
                branchedChunkList->RemoveOwningNode(branchedNode);
            }
        }
    }

    // Check if we have anything to do at all.
    if (branchedMode == NChunkClient::EUpdateMode::None) {
        return;
    }

    bool topmostCommit = !originatingNode->GetTransaction();
    auto newOriginatingMode = topmostCommit || originatingNode->GetType() == NObjectClient::EObjectType::Journal
        ? NChunkClient::EUpdateMode::None
        : originatingMode == NChunkClient::EUpdateMode::Overwrite || branchedMode == NChunkClient::EUpdateMode::Overwrite
            ? NChunkClient::EUpdateMode::Overwrite
            : NChunkClient::EUpdateMode::Append;

    // For new chunks, there're two reasons to update chunk requisition.
    //
    // 1) To ensure proper replicator behavior. This is only needed for topmost
    // commits, and only when nodes' replication settings differ.
    //
    // 2) To ensure proper resource accounting. This is necessary (A) for all
    // topmost commits (since committed and uncommitted resources are tracked
    // separately) and (B) for nested commits when replication changes (NB: node
    // accounts cannot be changed within transactions and are therefore
    // irrelevant).
    //
    // For old chunks, requisition update is only needed iff they're being
    // overwritten. (NB: replication settings changes are never merged back to
    // the originating node and thus have no effect on these chunks.)

    auto requisitionUpdateNeeded = topmostCommit || originatingNode->Replication() != branchedNode->Replication();

    // Below, chunk requisition update is scheduled no matter what (for non-external chunks,
    // of course). If nothing else, this is necessary to update 'committed' flags on chunks.

    if (branchedMode == NChunkClient::EUpdateMode::Overwrite) {
        if (!isExternal) {
            auto oldOriginatingChunkLists = originatingNode->GetChunkLists();
            if (branchedChunkList->GetKind() == EChunkListKind::Static || !originatingNode->IsTrunk()) {
                for (auto contentType : TEnumTraits<EChunkListContentType>::GetDomainValues()) {
                    auto* originatingChunkList = originatingNode->GetChunkList(contentType);
                    auto* branchedChunkList = branchedNode->GetChunkList(contentType);
                    if (!originatingChunkList) {
                        YT_VERIFY(!branchedChunkList);
                        continue;
                    }

                    originatingChunkList->RemoveOwningNode(originatingNode);
                    branchedChunkList->AddOwningNode(originatingNode);
                    originatingNode->SetChunkList(contentType, branchedChunkList);
                }
            } else {
                YT_VERIFY(branchedChunkList->GetKind() == EChunkListKind::SortedDynamicRoot);
                if (branchedChunkList != originatingChunkList) {
                    const auto& tabletManager = TBase::Bootstrap_->GetTabletManager();
                    tabletManager->MergeTable(
                        originatingNode->template As<TTableNode>(),
                        branchedNode->template As<TTableNode>());
                } else {
                    YT_LOG_ALERT(
                        "Branched chunk list equals originating chunk list "
                        "(UpdateMode: %v, ChunkListId: %v, NodeId: %v, TransactionId: %v)",
                        branchedMode,
                        branchedChunkList->GetId(),
                        originatingNode->GetId(),
                        branchedNode->GetTransaction()->GetId());
                }
            }

            for (auto contentType : TEnumTraits<EChunkListContentType>::GetDomainValues()) {
                if (auto* originatingChunkList = oldOriginatingChunkLists[contentType]) {
                    chunkManager->ScheduleChunkRequisitionUpdate(originatingChunkList);
                }

                if (requisitionUpdateNeeded) {
                    if (auto* branchedChunkList = branchedNode->GetChunkList(contentType)) {
                        chunkManager->ScheduleChunkRequisitionUpdate(branchedChunkList);
                    }
                }
            }
        }

        originatingNode->SnapshotStatistics() = branchedNode->SnapshotStatistics();
        originatingNode->DeltaStatistics() = branchedNode->DeltaStatistics();
        originatingNode->SnapshotSecurityTags() = branchedNode->SnapshotSecurityTags();
        originatingNode->DeltaSecurityTags() = branchedNode->DeltaSecurityTags();
        originatingNode->ChunkMergerTraversalInfo() = {0, 0};
    } else {
        YT_VERIFY(branchedMode == NChunkClient::EUpdateMode::Append);

        bool isDynamic = false;

        TEnumIndexedVector<EChunkListContentType, TChunkTree*> deltaTrees;
        TChunkLists originatingChunkLists;
        TChunkLists newOriginatingChunkLists;

        if (!isExternal) {
            if (branchedChunkList->GetKind() == EChunkListKind::SortedDynamicRoot) {
                if (originatingNode->IsTrunk()) {
                    if (branchedChunkList != originatingChunkList) {
                        const auto& tabletManager = TBase::Bootstrap_->GetTabletManager();
                        tabletManager->MergeTable(
                            originatingNode->template As<TTableNode>(),
                            branchedNode->template As<TTableNode>());
                    } else {
                        YT_LOG_ALERT(
                            "Branched chunk list equals originating chunk list "
                            "(UpdateMode: %v, ChunkListId: %v, NodeId: %v, TransactionId: %v)",
                            branchedMode,
                            branchedChunkList->GetId(),
                            originatingNode->GetId(),
                            branchedNode->GetTransaction()->GetId());
                    }
                } else {
                    // For non-trunk node just overwrite originating node with branched node contents.
                    // Could be made more consistent with static tables by using hierarchical chunk lists.

                    YT_VERIFY(originatingNode->GetHunkChunkList() == branchedNode->GetHunkChunkList());

                    originatingNode->SetChunkList(branchedChunkList);
                    originatingChunkList->RemoveOwningNode(originatingNode);
                    branchedChunkList->AddOwningNode(originatingNode);
                }
                isDynamic = true;
            } else {
                YT_VERIFY(branchedChunkList->GetKind() == EChunkListKind::Static);

                for (auto contentType : TEnumTraits<EChunkListContentType>::GetDomainValues()) {
                    auto* originatingChunkList = originatingNode->GetChunkList(contentType);
                    auto* branchedChunkList = branchedNode->GetChunkList(contentType);
                    if (!originatingChunkList) {
                        YT_VERIFY(!branchedChunkList);
                        continue;
                    }

                    YT_VERIFY(branchedChunkList->Children().size() == 2);
                    deltaTrees[contentType] = branchedChunkList->Children()[1];

                    auto* newOriginatingChunkList = chunkManager->CreateChunkList(originatingChunkList->GetKind());
                    originatingChunkLists[contentType] = originatingChunkList;
                    newOriginatingChunkLists[contentType] = newOriginatingChunkList;

                    originatingChunkList->RemoveOwningNode(originatingNode);
                    newOriginatingChunkList->AddOwningNode(originatingNode);
                    originatingNode->SetChunkList(contentType, newOriginatingChunkList);
                }
            }
        }

        if (originatingMode == NChunkClient::EUpdateMode::Append) {
            YT_VERIFY(!topmostCommit);
            if (!isExternal && branchedChunkList->GetKind() == EChunkListKind::Static) {
                for (auto contentType : TEnumTraits<EChunkListContentType>::GetDomainValues()) {
                    auto* originatingChunkList = originatingChunkLists[contentType];
                    auto* newOriginatingChunkList = newOriginatingChunkLists[contentType];
                    if (!originatingChunkList) {
                        YT_VERIFY(!newOriginatingChunkList);
                        continue;
                    }

                    chunkManager->AttachToChunkList(newOriginatingChunkList, originatingChunkList->Children()[0]);
                    auto* newDeltaChunkList = chunkManager->CreateChunkList(originatingChunkList->GetKind());
                    chunkManager->AttachToChunkList(newOriginatingChunkList, newDeltaChunkList);
                    chunkManager->AttachToChunkList(newDeltaChunkList, originatingChunkList->Children()[1]);
                    chunkManager->AttachToChunkList(newDeltaChunkList, deltaTrees[contentType]);
                }
            }

            originatingNode->DeltaStatistics() += branchedNode->DeltaStatistics();
            originatingNode->DeltaSecurityTags() = securityTagsRegistry->Intern(
                *originatingNode->DeltaSecurityTags() + *branchedNode->DeltaSecurityTags());
        } else {
            if (!isExternal && branchedChunkList->GetKind() == EChunkListKind::Static) {
                YT_VERIFY(originatingChunkList->GetKind() == EChunkListKind::Static);

                for (auto contentType : TEnumTraits<EChunkListContentType>::GetDomainValues()) {
                    auto* originatingChunkList = originatingChunkLists[contentType];
                    auto* newOriginatingChunkList = newOriginatingChunkLists[contentType];
                    if (!originatingChunkList) {
                        YT_VERIFY(!newOriginatingChunkList);
                        continue;
                    }

                    auto* deltaTree = deltaTrees[contentType];
                    chunkManager->AttachToChunkList(newOriginatingChunkList, originatingChunkList);
                    chunkManager->AttachToChunkList(newOriginatingChunkList, deltaTree);

                    if (requisitionUpdateNeeded) {
                        chunkManager->ScheduleChunkRequisitionUpdate(deltaTree);
                    }
                }
            }

            if (newOriginatingMode == NChunkClient::EUpdateMode::Append) {
                originatingNode->DeltaStatistics() += branchedNode->DeltaStatistics();
                originatingNode->DeltaSecurityTags() = securityTagsRegistry->Intern(
                    *originatingNode->DeltaSecurityTags() + *branchedNode->DeltaSecurityTags());
            } else {
                originatingNode->SnapshotStatistics() += branchedNode->DeltaStatistics();
                originatingNode->SnapshotSecurityTags() = securityTagsRegistry->Intern(
                    *originatingNode->SnapshotSecurityTags() + *branchedNode->DeltaSecurityTags());
            }
        }

        if (!isExternal && isDynamic) {
            const auto& tableManager = TBase::Bootstrap_->GetTableManager();
            tableManager->SendStatisticsUpdate(originatingNode);
        }
    }

    if (topmostCommit && !isExternal && branchedChunkList->GetKind() == EChunkListKind::Static) {
        // Rebalance when the topmost transaction commits.
        chunkManager->RebalanceChunkTree(originatingNode->GetChunkList(), EChunkTreeBalancerMode::Permissive);
        // Don't schedule requisition update for #newOriginatingChunkList here.
        // See balancer implementation for details.

        if (originatingNode->GetChunkMergerMode() != EChunkMergerMode::None) {
            chunkManager->ScheduleChunkMerge(originatingNode);
        }
    }

    originatingNode->SetUpdateMode(newOriginatingMode);
}

template <class TChunkOwner>
void TChunkOwnerTypeHandler<TChunkOwner>::DoLogMerge(
    TChunkOwner* originatingNode,
    TChunkOwner* branchedNode)
{
    const auto& chunkManager = TBase::Bootstrap_->GetChunkManager();
    const auto* originatingPrimaryMedium = chunkManager->GetMediumByIndex(originatingNode->GetPrimaryMediumIndex());
    const auto* branchedPrimaryMedium = chunkManager->GetMediumByIndex(branchedNode->GetPrimaryMediumIndex());
    YT_LOG_DEBUG_IF(
        TBase::IsMutationLoggingEnabled(),
        "Node merged (OriginatingNodeId: %v, OriginatingPrimaryMedium: %v, "
        "OriginatingReplication: %v, BranchedNodeId: %v, BranchedChunkListId: %v, "
        "BranchedHunkChunkListId: %v, BranchedUpdateMode: %v, BranchedPrimaryMedium: %v, "
        "BranchedReplication: %v, NewOriginatingChunkListId: %v, NewOriginatingHunkChunkListId: %v, "
        "NewOriginatingUpdateMode: %v, BranchedSnapshotStatistics: %v, BranchedDeltaStatistics: %v, "
        "NewOriginatingSnapshotStatistics: %v, NewOriginatingDeltaStatistics: %v)",
        originatingNode->GetVersionedId(),
        originatingPrimaryMedium->GetName(),
        originatingNode->Replication(),
        branchedNode->GetVersionedId(),
        NObjectServer::GetObjectId(branchedNode->GetChunkList()),
        NObjectServer::GetObjectId(branchedNode->GetHunkChunkList()),
        branchedNode->GetUpdateMode(),
        branchedPrimaryMedium->GetName(),
        branchedNode->Replication(),
        NObjectServer::GetObjectId(originatingNode->GetChunkList()),
        NObjectServer::GetObjectId(originatingNode->GetHunkChunkList()),
        originatingNode->GetUpdateMode(),
        branchedNode->SnapshotStatistics(),
        branchedNode->DeltaStatistics(),
        originatingNode->SnapshotStatistics(),
        originatingNode->DeltaStatistics());
}

template <class TChunkOwner>
void TChunkOwnerTypeHandler<TChunkOwner>::DoClone(
    TChunkOwner* sourceNode,
    TChunkOwner* clonedTrunkNode,
    ICypressNodeFactory* factory,
    ENodeCloneMode mode,
    TAccount* account)
{
    TBase::DoClone(sourceNode, clonedTrunkNode, factory, mode, account);

    clonedTrunkNode->SetPrimaryMediumIndex(sourceNode->GetPrimaryMediumIndex());
    clonedTrunkNode->Replication() = sourceNode->Replication();
    clonedTrunkNode->SnapshotStatistics() = sourceNode->SnapshotStatistics();
    clonedTrunkNode->DeltaStatistics() = sourceNode->DeltaStatistics();
    clonedTrunkNode->SnapshotSecurityTags() = sourceNode->SnapshotSecurityTags();
    clonedTrunkNode->DeltaSecurityTags() = sourceNode->DeltaSecurityTags();
    clonedTrunkNode->SetCompressionCodec(sourceNode->GetCompressionCodec());
    clonedTrunkNode->SetErasureCodec(sourceNode->GetErasureCodec());
    clonedTrunkNode->SetEnableStripedErasure(sourceNode->GetEnableStripedErasure());
    clonedTrunkNode->SetEnableSkynetSharing(sourceNode->GetEnableSkynetSharing());

    if (!sourceNode->IsExternal()) {
        for (auto contentType : TEnumTraits<EChunkListContentType>::GetDomainValues()) {
            auto* chunkList = sourceNode->GetChunkList(contentType);
            YT_VERIFY(!clonedTrunkNode->GetChunkList(contentType));
            clonedTrunkNode->SetChunkList(contentType, chunkList);
            if (chunkList) {
                chunkList->AddOwningNode(clonedTrunkNode);
                if (clonedTrunkNode->IsTrunk() && sourceNode->GetAccount() != clonedTrunkNode->GetAccount()) {
                    const auto& chunkManager = TBase::Bootstrap_->GetChunkManager();
                    chunkManager->ScheduleChunkRequisitionUpdate(chunkList);
                }
            }
        }
    }
}

template <class TChunkOwner>
void TChunkOwnerTypeHandler<TChunkOwner>::DoBeginCopy(
    TChunkOwner* node,
    TBeginCopyContext* context)
{
    if (!node->IsExternal()) {
        // TODO(babenko): support cross-cell copying for non-external nodes
        const auto& cypressManager = TBase::Bootstrap_->GetCypressManager();
        THROW_ERROR_EXCEPTION("Node %v must be external to support cross-cell copying",
            cypressManager->GetNodePath(node->GetTrunkNode(), context->GetTransaction()));
    }

    TBase::DoBeginCopy(node, context);

    using NYT::Save;

    const auto& chunkManager = this->Bootstrap_->GetChunkManager();
    auto* medium = chunkManager->GetMediumByIndexOrThrow(node->GetPrimaryMediumIndex());
    Save(*context, medium);

    Save(*context, node->Replication());
    Save(*context, node->SnapshotStatistics());
    Save(*context, node->DeltaStatistics());
    Save(*context, node->SnapshotSecurityTags());
    Save(*context, node->DeltaSecurityTags());
    Save(*context, node->GetCompressionCodec());
    Save(*context, node->GetErasureCodec());
    Save(*context, node->GetEnableStripedErasure());
    Save(*context, node->GetEnableSkynetSharing());

    context->RegisterExternalCellTag(node->GetExternalCellTag());
}

template <class TChunkOwner>
void TChunkOwnerTypeHandler<TChunkOwner>::DoEndCopy(
    TChunkOwner* trunkNode,
    TEndCopyContext* context,
    ICypressNodeFactory* factory)
{
    TBase::DoEndCopy(trunkNode, context, factory);

    using NYT::Load;

    auto* medium = Load<TMedium*>(*context);
    trunkNode->SetPrimaryMediumIndex(medium->GetIndex());

    Load(*context, trunkNode->Replication());
    Load(*context, trunkNode->SnapshotStatistics());
    Load(*context, trunkNode->DeltaStatistics());
    Load(*context, trunkNode->SnapshotSecurityTags());
    Load(*context, trunkNode->DeltaSecurityTags());
    trunkNode->SetCompressionCodec(Load<NCompression::ECodec>(*context));
    trunkNode->SetErasureCodec(Load<NErasure::ECodec>(*context));
    trunkNode->SetEnableStripedErasure(Load<bool>(*context));
    trunkNode->SetEnableSkynetSharing(Load<bool>(*context));
}

////////////////////////////////////////////////////////////////////////////////

template class TChunkOwnerTypeHandler<TFileNode>;
template class TChunkOwnerTypeHandler<TTableNode>;
template class TChunkOwnerTypeHandler<TReplicatedTableNode>;
template class TChunkOwnerTypeHandler<TJournalNode>;
template class TChunkOwnerTypeHandler<NTabletServer::THunkStorageNode>;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkServer
