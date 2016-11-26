#include "chunk_placement.h"
#include "private.h"
#include "chunk.h"
#include "chunk_manager.h"
#include "job.h"

#include <yt/server/cell_master/bootstrap.h>
#include <yt/server/cell_master/config.h>

#include <yt/server/node_tracker_server/node.h>
#include <yt/server/node_tracker_server/node_tracker.h>
#include <yt/server/node_tracker_server/rack.h>

#include <yt/server/object_server/object.h>

#include <yt/core/misc/small_set.h>

#include <util/random/random.h>

#include <array>

namespace NYT {
namespace NChunkServer {

using namespace NObjectClient;
using namespace NObjectServer;
using namespace NChunkClient;
using namespace NNodeTrackerServer;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

class TChunkPlacement::TTargetCollector
{
public:
    TTargetCollector(
        const TChunk* chunk,
        int mediumIndex,
        int maxReplicasPerRack,
        const TNodeList* forbiddenNodes)
        : MaxReplicasPerRack_(maxReplicasPerRack)
    {
        if (forbiddenNodes) {
            ForbiddenNodes_ = *forbiddenNodes;
        }

        for (auto replica : chunk->StoredReplicas()) {
            if (replica.GetMediumIndex() == mediumIndex) {
                auto* node = replica.GetPtr();
                IncreaseRackUsage(node);
                ForbiddenNodes_.push_back(node);
            }
        }

        std::sort(ForbiddenNodes_.begin(), ForbiddenNodes_.end());
    }

    bool CheckNode(TNode* node, bool enableRackAwareness) const
    {
        if (std::find(ForbiddenNodes_.begin(), ForbiddenNodes_.end(), node) != ForbiddenNodes_.end()) {
            return false;
        }

        if (enableRackAwareness) {
            const auto* rack = node->GetRack();
            if (rack && PerRackCounters_[rack->GetIndex()] >= MaxReplicasPerRack_) {
                return false;
            }
        }

        return true;
    }

    void AddNode(TNode* node)
    {
        IncreaseRackUsage(node);
        AddedNodes_.push_back(node);
        ForbiddenNodes_.push_back(node);
    }

    const TNodeList& GetAddedNodes() const
    {
        return AddedNodes_;
    }

private:
    const int MaxReplicasPerRack_;

    std::array<i8, MaxRackCount> PerRackCounters_{};
    TNodeList ForbiddenNodes_;
    TNodeList AddedNodes_;


    void IncreaseRackUsage(TNode* node)
    {
        const auto* rack = node->GetRack();
        if (rack) {
            ++PerRackCounters_[rack->GetIndex()];
        }
    }

};

////////////////////////////////////////////////////////////////////////////////

TChunkPlacement::TChunkPlacement(
    TChunkManagerConfigPtr config,
    TBootstrap* bootstrap)
    : Config_(config)
    , Bootstrap_(bootstrap)
{
    YCHECK(Config_);
    YCHECK(Bootstrap_);
}

void TChunkPlacement::OnNodeRegistered(TNode* node)
{
    if (node->GetLocalState() != ENodeState::Registered &&
        node->GetLocalState() != ENodeState::Online)
        return;

    InsertToLoadFactorMaps(node);
    InsertToFillFactorMaps(node);
}

void TChunkPlacement::OnNodeUnregistered(TNode* node)
{
    RemoveFromLoadFactorMaps(node);

    RemoveFromFillFactorMaps(node);
}

void TChunkPlacement::OnNodeUpdated(TNode* node)
{
    node->ClearSessionHints();

    const auto& chunkManager = Bootstrap_->GetChunkManager();

    // Recompute IO weight.
    // Currently its just the number of non-full locations.
    TPerMediumArray<double> ioWeights;
    ioWeights.fill(0.0);
    for (const auto& location : node->Statistics().locations()) {
        if (location.full()) {
            continue;
        }

        auto* medium = chunkManager->FindMediumByIndex(location.medium_index());
        if (!medium || medium->GetCache()) {
            continue;
        }

        ioWeights[location.medium_index()] += 1.0;
    }
    node->IOWeights() = ioWeights;

    OnNodeUnregistered(node);
    OnNodeRegistered(node);
}

void TChunkPlacement::OnNodeDisposed(TNode* node)
{
    for (const auto& pair : Bootstrap_->GetChunkManager()->Media()) {
        auto mediumIndex = pair.second->GetIndex();
        YCHECK(!node->GetLoadFactorIterator(mediumIndex));
        YCHECK(!node->GetFillFactorIterator(mediumIndex));
    }
}

TNodeList TChunkPlacement::AllocateWriteTargets(
    int mediumIndex,
    TChunk* chunk,
    int desiredCount,
    int minCount,
    TNullable<int> replicationFactorOverride,
    const TNodeList* forbiddenNodes,
    const TNullable<Stroka>& preferredHostName,
    ESessionType sessionType)
{
    auto targetNodes = GetWriteTargets(
        mediumIndex,
        chunk,
        desiredCount,
        minCount,
        sessionType == ESessionType::Replication,
        replicationFactorOverride,
        forbiddenNodes,
        preferredHostName);

    for (auto* target : targetNodes) {
        AddSessionHint(target, sessionType);
    }

    return targetNodes;
}

void TChunkPlacement::InsertToFillFactorMaps(TNode* node)
{
    RemoveFromFillFactorMaps(node);

    for (const auto& pair : Bootstrap_->GetChunkManager()->Media()) {
        auto mediumIndex = pair.second->GetIndex();
 
        if (!IsValidBalancingTarget(mediumIndex, node)) {
            continue;
        }

        auto fillFactor = node->GetFillFactor(mediumIndex);
        if (!fillFactor) {
            continue;
        }

        auto it = MediumToFillFactorToNode_[mediumIndex].emplace(*fillFactor, node);
        node->SetFillFactorIterator(mediumIndex, it);
    }
}

void TChunkPlacement::RemoveFromFillFactorMaps(TNode* node)
{
    for (const auto& pair : Bootstrap_->GetChunkManager()->Media()) {
        auto mediumIndex = pair.second->GetIndex();
        auto it = node->GetFillFactorIterator(mediumIndex);
        if (!it) {
            continue;
        }

        MediumToFillFactorToNode_[mediumIndex].erase(*it);
        node->SetFillFactorIterator(mediumIndex, Null);
    }
}

void TChunkPlacement::InsertToLoadFactorMaps(TNode* node)
{
    RemoveFromLoadFactorMaps(node);

    for (const auto& pair : Bootstrap_->GetChunkManager()->Media()) {
        auto mediumIndex = pair.second->GetIndex();
        
        if (!IsValidWriteTarget(mediumIndex, node)) {
            continue;
        }

        auto loadFactor = node->GetLoadFactor(mediumIndex);
        if (!loadFactor) {
            continue;
        }

        auto it = MediumToLoadFactorToNode_[mediumIndex].emplace(*loadFactor, node);
        node->SetLoadFactorIterator(mediumIndex, it);
    }
}

void TChunkPlacement::RemoveFromLoadFactorMaps(TNode* node)
{
    for (const auto& pair : Bootstrap_->GetChunkManager()->Media()) {
        auto mediumIndex = pair.second->GetIndex();
        auto it = node->GetLoadFactorIterator(mediumIndex);
        if (!it) {
            continue;
        }

        MediumToLoadFactorToNode_[mediumIndex].erase(*it);
        node->SetLoadFactorIterator(mediumIndex, Null);
    }
}

TNodeList TChunkPlacement::GetWriteTargets(
    int mediumIndex,
    TChunk* chunk,
    int desiredCount,
    int minCount,
    bool forceRackAwareness,
    TNullable<int> replicationFactorOverride,
    const TNodeList* forbiddenNodes,
    const TNullable<Stroka>& preferredHostName)
{
    int maxReplicasPerRack = GetMaxReplicasPerRack(chunk, mediumIndex, replicationFactorOverride);
    TTargetCollector collector(chunk, mediumIndex, maxReplicasPerRack, forbiddenNodes);

    auto tryAdd = [&] (TNode* node, bool enableRackAwareness) {
        if (IsValidWriteTarget(mediumIndex, node, chunk->GetType(), &collector, enableRackAwareness)) {
            collector.AddNode(node);
        }
    };

    auto tryAddAll = [&] (bool enableRackAwareness) {
        for (const auto& pair : MediumToLoadFactorToNode_[mediumIndex]) {
            auto* node = pair.second;
            if (collector.GetAddedNodes().size() == desiredCount)
                break;
            tryAdd(node, enableRackAwareness);
        }
    };

    if (preferredHostName) {
        const auto& nodeTracker = Bootstrap_->GetNodeTracker();
        auto* preferredNode = nodeTracker->FindNodeByHostName(*preferredHostName);
        if (preferredNode) {
            tryAdd(preferredNode, true);
        }
    }

    tryAddAll(true);
    if (!forceRackAwareness) {
        tryAddAll(false);
    }

    const auto& nodes = collector.GetAddedNodes();
    return nodes.size() < minCount ? TNodeList() : nodes;
}

TNodeList TChunkPlacement::AllocateWriteTargets(
    int mediumIndex,
    TChunk* chunk,
    int desiredCount,
    int minCount,
    TNullable<int> replicationFactorOverride,
    ESessionType sessionType)
{
    auto targetNodes = GetWriteTargets(
        mediumIndex,
        chunk,
        desiredCount,
        minCount,
        sessionType == ESessionType::Replication,
        replicationFactorOverride);

    for (auto* target : targetNodes) {
        AddSessionHint(target, sessionType);
    }

    return targetNodes;
}

TNode* TChunkPlacement::GetRemovalTarget(TChunkPtrWithIndexes chunkWithIndexes)
{
    auto* chunk = chunkWithIndexes.GetPtr();
    auto replicaIndex = chunkWithIndexes.GetReplicaIndex();
    auto mediumIndex = chunkWithIndexes.GetMediumIndex();
    int maxReplicasPerRack = GetMaxReplicasPerRack(chunk, mediumIndex, Null);

    std::array<i8, MaxRackCount> perRackCounters{};
    for (auto replica : chunk->StoredReplicas()) {
        if (replica.GetMediumIndex() != mediumIndex)
            continue;

        const auto* rack = replica.GetPtr()->GetRack();
        if (rack) {
            ++perRackCounters[rack->GetIndex()];
        }
    }

    // An arbitrary node from a rack with too many replicas.
    TNode* rackWinner = nullptr;
    // A node with the largest fill factor.
    TNode* fillFactorWinner = nullptr;

    for (auto replica : chunk->StoredReplicas()) {
        if ((replica.GetMediumIndex() == mediumIndex) &&
            (chunk->IsRegular() ||
             chunk->IsErasure() && replica.GetReplicaIndex() == replicaIndex ||
             chunk->IsJournal())) // allow removing arbitrary journal replicas
        {
            auto* node = replica.GetPtr();
            if (!IsValidRemovalTarget(node))
                continue;

            const auto* rack = node->GetRack();
            if (rack && perRackCounters[rack->GetIndex()] > maxReplicasPerRack) {
                rackWinner = node;
            }

            auto nodeFillFactor = node->GetFillFactor(mediumIndex);

            if (nodeFillFactor &&
                (!fillFactorWinner ||
                 *nodeFillFactor > *fillFactorWinner->GetFillFactor(mediumIndex)))
            {
                fillFactorWinner = node;
            }
        }
    }

    return rackWinner ? rackWinner : fillFactorWinner;
}

bool TChunkPlacement::HasBalancingTargets(int mediumIndex, double maxFillFactor)
{
    if (maxFillFactor < 0) {
        return false;
    }

    if (MediumToFillFactorToNode_[mediumIndex].empty()) {
        return false;
    }

    auto* node = MediumToFillFactorToNode_[mediumIndex].begin()->second;
    auto nodeFillFactor = node->GetFillFactor(mediumIndex);
    YCHECK(nodeFillFactor);
    return *nodeFillFactor < maxFillFactor;
}

TNode* TChunkPlacement::AllocateBalancingTarget(
    int mediumIndex,
    TChunk* chunk,
    double maxFillFactor)
{
    auto* target = GetBalancingTarget(mediumIndex, chunk, maxFillFactor);

    if (target) {
        AddSessionHint(target, ESessionType::Replication);
    }

    return target;
}

TNode* TChunkPlacement::GetBalancingTarget(
    int mediumIndex,
    TChunk* chunk,
    double maxFillFactor)
{
    int maxReplicasPerRack = GetMaxReplicasPerRack(chunk, mediumIndex, Null);
    TTargetCollector collector(chunk, mediumIndex, maxReplicasPerRack, nullptr);

    for (const auto& pair : MediumToFillFactorToNode_[mediumIndex]) {
        auto* node = pair.second;
        auto nodeFillFactor = node->GetFillFactor(mediumIndex);
        YCHECK(nodeFillFactor);
        if (*nodeFillFactor > maxFillFactor) {
            break;
        }
        if (IsValidBalancingTarget(mediumIndex, node, chunk->GetType(), &collector, true)) {
            return node;
        }
    }

    return nullptr;
}

bool TChunkPlacement::IsValidWriteTarget(
    int mediumIndex,
    TNode* node)
{
    if (node->GetLocalState() != ENodeState::Online) {
        // Do not write anything to a node before its first heartbeat or after it is unregistered.
        return false;
    }

    if (node->IsFull(mediumIndex)) {
        // Do not write anything to full nodes.
        return false;
    }
    
    if (node->GetDecommissioned()) {
        // Do not write anything to decommissioned nodes.
        return false;
    }

    if (node->GetDisableWriteSessions()) {
        // Do not start new sessions if they are explicitly disabled.
        return false;
    }

    // Seems OK :)
    return true;
}

bool TChunkPlacement::IsValidWriteTarget(
    int mediumIndex,
    TNode* node,
    EObjectType chunkType,
    TTargetCollector* collector,
    bool enableRackAwareness)
{
    // Check node first.
    if (!IsValidWriteTarget(mediumIndex, node)) {
        return false;
    }

    if (!IsAcceptedChunkType(mediumIndex, node, chunkType)) {
        // Do not write anything to nodes not accepting this type of chunks.
        return false;
    }

    auto* medium = Bootstrap_->GetChunkManager()->FindMediumByIndex(mediumIndex);
    if (!medium || medium->GetCache()) {
        // Direct writing to cache locations is not allowed.
        return false;
    }

    if (!collector->CheckNode(node, enableRackAwareness)) {
        // The collector does not like this node.
        return false;
    }

    // Seems OK :)
    return true;
}

bool TChunkPlacement::IsValidBalancingTarget(
    int mediumIndex,
    TNode* node)
{
    // Balancing implies write, after all.
    if (!IsValidWriteTarget(mediumIndex, node)) {
        return false;
    }

    if (node->GetSessionCount(ESessionType::Replication) >= Config_->MaxReplicationWriteSessions) {
        // Do not write anything to a node with too many write sessions.
        return false;
    }

    // Seems OK :)
    return true;
}

bool TChunkPlacement::IsValidBalancingTarget(
    int mediumIndex,
    TNode* node,
    NObjectClient::EObjectType chunkType,
    TTargetCollector* collector,
    bool enableRackAwareness)
{
    // Check node first.
    if (!IsValidWriteTarget(mediumIndex, node)) {
        return false;
    }

    // Balancing implies write, after all.
    if (!IsValidWriteTarget(mediumIndex, node, chunkType, collector, enableRackAwareness)) {
        return false;
    }

    // Seems OK :)
    return true;
}

bool TChunkPlacement::IsValidRemovalTarget(TNode* node)
{
    if (node->GetLocalState() != ENodeState::Online) {
        // Do not remove anything from a node before its first heartbeat or after it is unregistered.
        return false;
    }

    return true;
}

std::vector<TChunkPtrWithIndexes> TChunkPlacement::GetBalancingChunks(
    int mediumIndex,
    TNode* node,
    int replicaCount)
{
    const auto& objectManager = Bootstrap_->GetObjectManager();
    auto epoch = objectManager->GetCurrentEpoch();

    std::vector<TChunkPtrWithIndexes> result;
    result.reserve(replicaCount);

    // Let's bound the number of iterations somehow.
    // Never consider more chunks than the node has to avoid going into a loop (cf. YT-4258).
    int iterationCount = std::min(replicaCount * 2, static_cast<int>(node->StoredReplicas().size()));
    for (int index = 0; index < iterationCount; ++index) {
        auto replica = node->PickRandomReplica(mediumIndex);
        Y_ASSERT(replica.GetMediumIndex() == mediumIndex);
        auto* chunk = replica.GetPtr();
        if (!IsObjectAlive(chunk)) {
            break;
        }
        if (static_cast<int>(result.size()) >= replicaCount) {
            break;
        }
        if (!chunk->GetMovable()) {
            continue;
        }
        if (!chunk->IsSealed()) {
            continue;
        }
        if (chunk->GetScanFlag(EChunkScanKind::Refresh, epoch)) {
            continue;
        }
        if (chunk->IsJobScheduled()) {
            continue;
        }
        result.push_back(replica);
    }

    return result;
}

bool TChunkPlacement::IsAcceptedChunkType(int mediumIndex, TNode* node, EObjectType type)
{
    for (auto acceptedType : node->Statistics().accepted_chunk_types()) {
        if (acceptedType.medium_index() == mediumIndex &&
            EObjectType(acceptedType.chunk_type()) == type)
        {
            return true;
        }
    }
    return false;
}

void TChunkPlacement::AddSessionHint(TNode* node, ESessionType sessionType)
{
    node->AddSessionHint(sessionType);

    RemoveFromLoadFactorMaps(node);
    InsertToLoadFactorMaps(node);

    if (node->GetSessionCount(ESessionType::Replication) >= Config_->MaxReplicationWriteSessions) {
        RemoveFromFillFactorMaps(node);
    }
}

int TChunkPlacement::GetMaxReplicasPerRack(
    TChunk* chunk,
    int mediumIndex,
    TNullable<int> replicationFactorOverride)
{
    int result = chunk->GetMaxReplicasPerRack(mediumIndex, replicationFactorOverride);
    result = std::min(result, Config_->MaxReplicasPerRack);
    switch (chunk->GetType()) {
        case EObjectType::Chunk:         result = std::min(result, Config_->MaxRegularReplicasPerRack); break;
        case EObjectType::ErasureChunk:  result = std::min(result, Config_->MaxErasureReplicasPerRack); break;
        case EObjectType::JournalChunk:  result = std::min(result, Config_->MaxJournalReplicasPerRack); break;
        default:                         Y_UNREACHABLE();
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
