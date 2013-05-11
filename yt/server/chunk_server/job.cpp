#include "stdafx.h"
#include "job.h"

namespace NYT {
namespace NChunkServer {

using namespace NNodeTrackerServer;
using namespace NNodeTrackerClient;
using namespace NNodeTrackerClient::NProto;

////////////////////////////////////////////////////////////////////////////////

TJob::TJob(
    EJobType type,
    const TJobId& jobId,
    const TChunkIdWithIndex& chunkIdWithIndex,
    TNode* node,
    const std::vector<Stroka>& targetAddresses,
    const NErasure::TPartIndexList& erasedIndexes,
    TInstant startTime,
    const TNodeResources& resourceUsage)
    : JobId_(jobId)
    , Type_(type)
    , ChunkIdWithIndex_(chunkIdWithIndex)
    , Node_(node)
    , TargetAddresses_(targetAddresses)
    , ErasedIndexes_(erasedIndexes)
    , StartTime_(startTime)
    , ResourceUsage_(resourceUsage)
    , State_(EJobState::Running)
{ }

TJobPtr TJob::CreateForeign(
    const TJobId& jobId,
    const NNodeTrackerClient::NProto::TNodeResources& resourceUsage)
{
    return New<TJob>(
        EJobType::Foreign,
        jobId,
        TChunkIdWithIndex(NullChunkId, 0),
        static_cast<TNode*>(nullptr),
        std::vector<Stroka>(),
        NErasure::TPartIndexList(),
        TInstant::Zero(),
        resourceUsage);
}

TJobPtr TJob::CreateReplicate(
    const TChunkIdWithIndex& chunkIdWithIndex,
    TNode* node,
    const std::vector<Stroka>& targetAddresses,
    const NNodeTrackerClient::NProto::TNodeResources& resourceUsage)
{
    return New<TJob>(
        EJobType::ReplicateChunk,
        TJobId::Create(),
        chunkIdWithIndex,
        node,
        targetAddresses,
        NErasure::TPartIndexList(),
        TInstant::Now(),
        resourceUsage);
}

TJobPtr TJob::CreateRemove(
    const TChunkIdWithIndex& chunkIdWithIndex,
    NNodeTrackerServer::TNode* node,
    const NNodeTrackerClient::NProto::TNodeResources& resourceUsage)
{
    return New<TJob>(
        EJobType::RemoveChunk,
        TJobId::Create(),
        chunkIdWithIndex,
        node,
        std::vector<Stroka>(),
        NErasure::TPartIndexList(),
        TInstant::Now(),
        resourceUsage);
}

TJobPtr TJob::CreateRepair(
    const TChunkId& chunkId,
    NNodeTrackerServer::TNode* node,
    const std::vector<Stroka>& targetAddresses,
    const NErasure::TPartIndexList& erasedIndexes,
    const NNodeTrackerClient::NProto::TNodeResources& resourceUsage)
{
    return New<TJob>(
        EJobType::RepairChunk,
        TJobId::Create(),
        TChunkIdWithIndex(chunkId, 0),
        node,
        targetAddresses,
        erasedIndexes,
        TInstant::Now(),
        resourceUsage);
}

////////////////////////////////////////////////////////////////////////////////

TJobList::TJobList(const TChunkId& chunkId)
    : ChunkId_(chunkId)
{ }

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
