#include "chunk_replication.h"

#include "../misc/foreach.h"
#include "../misc/serialize.h"
#include "../misc/string.h"

namespace NYT {
namespace NChunkManager {

////////////////////////////////////////////////////////////////////////////////

// TODO: make configurable
static int MaxReplicationJobsPerHolder = 4;
static int MaxRemovalJobsPerHolder = 16;
static TDuration ChunkRefreshDelay = TDuration::Seconds(15);
static TDuration ChunkRefreshQuantum = TDuration::MilliSeconds(100);
static int MaxChunksPerRefresh = 1000;

static NLog::TLogger& Logger = ChunkManagerLogger;

////////////////////////////////////////////////////////////////////////////////

TChunkReplication::TChunkReplication(
    TChunkManager::TPtr chunkManager,
    TChunkPlacement::TPtr chunkPlacement)
    : ChunkManager(chunkManager)
    , ChunkPlacement(chunkPlacement)
{ }

void TChunkReplication::RunJobControl(
    const THolder& holder,
    const yvector<NProto::TJobInfo>& runningJobs,
    yvector<NProto::TJobStartInfo>* jobsToStart,
    yvector<TJobId>* jobsToStop )
{
    int replicationJobCount;
    int removalJobCount;
    ProcessRunningJobs(
        holder,
        runningJobs,
        jobsToStop,
        &replicationJobCount,
        &removalJobCount);

    ScheduleJobs(
        holder,
        Max(0, MaxReplicationJobsPerHolder - replicationJobCount),
        Max(0, MaxRemovalJobsPerHolder - removalJobCount),
        jobsToStart);
}

void TChunkReplication::RegisterHolder(const THolder& holder)
{
    YVERIFY(HolderInfoMap.insert(MakePair(holder.Id, THolderInfo())).Second());

    FOREACH(const auto& chunk, holder.Chunks) {
        ScheduleRefresh(chunk);
    }
}

void TChunkReplication::UnregisterHolder(const THolder& holder)
{
    YVERIFY(HolderInfoMap.erase(holder.Id) == 1);
}

void TChunkReplication::RegisterReplica(const THolder& holder, const TChunk& chunk)
{
    UNUSED(holder);
    ScheduleRefresh(chunk.Id);
}

void TChunkReplication::UnregisterReplica(const THolder& holder, const TChunk& chunk)
{
    UNUSED(holder);
    ScheduleRefresh(chunk.Id);
}

void TChunkReplication::ProcessRunningJobs(
    const THolder& holder,
    const yvector<NProto::TJobInfo>& runningJobs,
    yvector<TJobId>* jobsToStop,
    int* replicationJobCount,
    int* removalJobCount)
{
    *replicationJobCount = 0;
    *removalJobCount = 0;

    // TODO: check for missing jobs
    FOREACH(const auto& jobInfo, runningJobs) {
        auto jobId = TJobId::FromProto(jobInfo.GetJobId());
        const auto& job = ChunkManager->GetJob(jobId);
        auto jobState = EJobState(jobInfo.GetState());
        switch (jobState) {
        case EJobState::Running:
            switch (job.Type) {
                case EJobType::Replicate:
                    ++*replicationJobCount;
                    break;

                case EJobType::Remove:
                    ++*removalJobCount;
                    break;

                default:
                    YASSERT(false);
                    break;
            }
            LOG_INFO("Job running (JobId: %s, HolderId: %d)",
                ~jobId.ToString(),
                holder.Id);
            break;

        case EJobState::Completed:
            jobsToStop->push_back(jobId);
            LOG_INFO("Job completed (JobId: %s, HolderId: %d)",
                ~jobId.ToString(),
                holder.Id);
            break;

        case EJobState::Failed:
            jobsToStop->push_back(jobId);
            LOG_WARNING("Job failed (JobId: %s, HolderId: %d)",
                ~jobId.ToString(),
                holder.Id);
            break;

        default:
            YASSERT(false);
            break;
        }
    }
}

bool TChunkReplication::IsRefreshScheduled(const TChunkId& chunkId)
{
    return RefreshSet.find(chunkId) != RefreshSet.end();
}

yvector<Stroka> TChunkReplication::GetTargetAddresses(
    const TChunk& chunk,
    int replicaCount)
{
    yhash_set<Stroka> forbiddenAddresses;

    FOREACH(auto holderId, chunk.Locations) {
        const auto& holder = ChunkManager->GetHolder(holderId);
        forbiddenAddresses.insert(holder.Address);
    }

    const auto* jobList = ChunkManager->FindJobList(chunk.Id);
    if (jobList != NULL) {
        FOREACH(const auto& jobId, jobList->Jobs) {
            const auto& job = ChunkManager->GetJob(jobId);
            if (job.Type == EJobType::Replicate && job.ChunkId == chunk.Id) {
                forbiddenAddresses.insert(job.TargetAddresses.begin(), job.TargetAddresses.end());
            }
        }
    }

    auto candidateHolders = ChunkPlacement->GetTargetHolders(replicaCount + forbiddenAddresses.size());

    yvector<Stroka> targetAddresses;
    FOREACH(auto holderId, candidateHolders) {
        if (targetAddresses.ysize() >= replicaCount)
            break;

        const auto& holder = ChunkManager->GetHolder(holderId);
        if (forbiddenAddresses.find(holder.Address) == forbiddenAddresses.end()) {
            targetAddresses.push_back(holder.Address);
        }
    }

    return targetAddresses;
}

TChunkReplication::EScheduleFlags TChunkReplication::ScheduleReplicationJob(
    const THolder& holder,
    const TChunkId& chunkId,
    yvector<NProto::TJobStartInfo>* jobsToStart)
{
    const auto* chunk = ChunkManager->FindChunk(chunkId);
    if (chunk == NULL) {
        LOG_INFO("Chunk for replication is missing (ChunkId: %s, HolderId: %d)",
            ~chunkId.ToString(),
            holder.Id);
        return EScheduleFlags::Purged;
    }

    if (IsRefreshScheduled(chunkId)) {
        LOG_INFO("Chunk for replication is scheduled for another refresh (ChunkId: %s, HolderId: %d)",
            ~chunkId.ToString(),
            holder.Id);
        return EScheduleFlags::None;
    }

    int desiredCount;
    int realCount;
    int plusCount;
    int minusCount;
    GetReplicaStatistics(
        *chunk,
        &desiredCount,
        &realCount,
        &plusCount,
        &minusCount);

    int requestedCount = desiredCount - (realCount + plusCount);
    if (requestedCount <= 0) {
        // TODO: is this possible?
        LOG_INFO("Chunk for replication has enough replicas (ChunkId: %s, HolderId: %d)",
            ~chunkId.ToString(),
            holder.Id);
        return EScheduleFlags::Purged;
    }

    auto targetAddresses = GetTargetAddresses(*chunk, requestedCount);
    if (targetAddresses.empty()) {
        LOG_INFO("No suitable target holders for replication (ChunkId: %s, HolderId: %d)",
            ~chunkId.ToString(),
            holder.Id);
        return EScheduleFlags::None;
    }

    auto jobId = TJobId::Create();
    NProto::TJobStartInfo startInfo;
    startInfo.SetJobId(jobId.ToProto());
    startInfo.SetType(EJobType::Replicate);
    startInfo.SetChunkId(chunkId.ToProto());
    ToProto(*startInfo.MutableTargetAddresses(), targetAddresses);
    jobsToStart->push_back(startInfo);

    LOG_INFO("Chunk replication scheduled (ChunkId: %s, HolderId: %d, JobId: %s, TargetAddresses: [%s])",
        ~chunkId.ToString(),
        holder.Id,
        ~jobId.ToString(),
        ~JoinToString(targetAddresses));

    return
        targetAddresses.ysize() == requestedCount
        // TODO: flagged enums
        ? (EScheduleFlags) (EScheduleFlags::Purged | EScheduleFlags::Scheduled)
        : (EScheduleFlags) EScheduleFlags::Scheduled;
}

TChunkReplication::EScheduleFlags TChunkReplication::ScheduleRemovalJob(
    const THolder& holder,
    const TChunkId& chunkId,
    yvector<NProto::TJobStartInfo>* jobsToStart )
{
    const auto* chunk = ChunkManager->FindChunk(chunkId);
    if (chunk == NULL) {
        LOG_INFO("Chunk for removal is missing (ChunkId: %s, HolderId: %d)",
            ~chunkId.ToString(),
            holder.Id);
        return EScheduleFlags::Purged;
    }

    if (IsRefreshScheduled(chunkId)) {
        LOG_INFO("Chunk for removal is scheduled for another refresh (ChunkId: %s, HolderId: %d)",
            ~chunkId.ToString(),
            holder.Id);
        return EScheduleFlags::None;
    }
    
    auto jobId = TJobId::Create();
    NProto::TJobStartInfo startInfo;
    startInfo.SetJobId(jobId.ToProto());
    startInfo.SetType(EJobType::Remove);
    startInfo.SetChunkId(chunkId.ToProto());
    jobsToStart->push_back(startInfo);

    LOG_INFO("Removal job scheduled (ChunkId: %s, HolderId: %d, JobId: %s)",
        ~chunkId.ToString(),
        holder.Id,
        ~jobId.ToString());

    // TODO: flagged enums
    return (EScheduleFlags) (EScheduleFlags::Purged | EScheduleFlags::Scheduled);
}

void TChunkReplication::ScheduleJobs(
    const THolder& holder,
    int maxReplicationJobsToStart,
    int maxRemovalJobsToStart,
    yvector<NProto::TJobStartInfo>* jobsToStart)
{
    auto* holderInfo = FindHolderInfo(holder.Id);
    if (holderInfo == NULL)
        return;

    {
        auto& chunksToReplicate = holderInfo->ChunksToReplicate;
        auto it = chunksToReplicate.begin();
        while (it != chunksToReplicate.end() && maxReplicationJobsToStart > 0) {
            auto jt = it;
            ++jt;
            const auto& chunkId = *it;
            auto flags = ScheduleReplicationJob(holder, chunkId, jobsToStart);
            if (flags & EScheduleFlags::Scheduled) {
                --maxReplicationJobsToStart;
            }
            if (flags & EScheduleFlags::Purged) {
                chunksToReplicate.erase(it);
            }
            it = jt;
        }
    }

    {
        auto& chunksToRemove = holderInfo->ChunksToRemove;
        auto it = chunksToRemove.begin();
        while (it != chunksToRemove.end() && maxRemovalJobsToStart > 0) {
            const auto& chunkId = *it;
            auto jt = it;
            ++jt;
            auto flags = ScheduleRemovalJob(holder, chunkId, jobsToStart);
            if (flags & EScheduleFlags::Scheduled) {
                --maxReplicationJobsToStart;
            }
            if (flags & EScheduleFlags::Purged) {
                chunksToRemove.erase(it);
            }
            it = jt;
        }
    }
}

void TChunkReplication::GetReplicaStatistics(
    const TChunk& chunk,
    int* desiredCount,
    int* realCount,
    int* plusCount,
    int* minusCount)
{
    *desiredCount = GetDesiredReplicaCount(chunk);
    *realCount = chunk.Locations.ysize();
    *plusCount = 0;
    *minusCount = 0;

    if (*realCount == 0) {
        return;
    }

    const auto* jobList = ChunkManager->FindJobList(chunk.Id);
    if (jobList != NULL) {
        yhash_set<Stroka> realAddresses(*realCount);
        FOREACH(auto holderId, chunk.Locations) {
            const auto& holder = ChunkManager->GetHolder(holderId);
            realAddresses.insert(holder.Address);
        }

        FOREACH(const auto& jobId, jobList->Jobs) {
            const auto& job = ChunkManager->GetJob(jobId);
            switch (job.Type) {
                case EJobType::Replicate: {
                    FOREACH(const auto& address, job.TargetAddresses) {
                        if (realAddresses.find(address) == realAddresses.end()) {
                            ++*plusCount;
                        }
                    }
                    break;
                }

                case EJobType::Remove:
                    if (realAddresses.find(job.RunnerAddress) != realAddresses.end()) {
                        ++*minusCount;
                    }
                    break;

                default:
                    YASSERT(false);
                    break;
                }
        }
    }
}

int TChunkReplication::GetDesiredReplicaCount(const TChunk& chunk)
{
    // TODO: make configurable
    UNUSED(chunk);
    return 3;
}

void TChunkReplication::Refresh(const TChunk& chunk)
{
    int desiredCount;
    int realCount;
    int plusCount;
    int minusCount;
    GetReplicaStatistics(
        chunk,
        &desiredCount,
        &realCount,
        &plusCount,
        &minusCount);

    FOREACH(auto holderId, chunk.Locations) {
        auto* holderInfo = FindHolderInfo(holderId);
        if (holderInfo != NULL) {
            holderInfo->ChunksToReplicate.erase(chunk.Id);
            holderInfo->ChunksToRemove.erase(chunk.Id);
        }
    }

    if (realCount == 0) {
        LOG_INFO("Chunk is lost (ChunkId: %s, ReplicaCount: %d+%d-%d, DesiredReplicaCount: %d)",
            ~chunk.Id.ToString(),
            realCount,
            plusCount,
            minusCount,
            desiredCount);
    } else if (realCount - minusCount > desiredCount) {
        // NB: never start removal jobs if new replicas are on the way, hence the check plusCount > 0.
        if (plusCount > 0) {
            LOG_INFO("Chunk is over-replicated, waiting for pending replications to complete (ChunkId: %s, ReplicaCount: %d+%d-%d, DesiredReplicaCount: %d)",
                ~chunk.Id.ToString(),
                realCount,
                plusCount,
                minusCount,
                desiredCount);
            return;
        }

        auto holderIds = GetHoldersForRemoval(chunk, realCount - minusCount - desiredCount);
        FOREACH(auto holderId, holderIds) {
            auto& holderInfo = GetHolderInfo(holderId);
            holderInfo.ChunksToRemove.insert(chunk.Id);
        }

        yvector<Stroka> holderAddresses;
        FOREACH(auto holderId, holderIds) {
            const auto& holder = ChunkManager->GetHolder(holderId);
            holderAddresses.push_back(holder.Address);
        }

        LOG_INFO("Chunk is over-replicated, removal is scheduled at [%s] (ChunkId: %s, ReplicaCount: %d+%d-%d, DesiredReplicaCount: %d)",
            ~JoinToString(holderAddresses),
            ~chunk.Id.ToString(),
            realCount,
            plusCount,
            minusCount,
            desiredCount);
    } else if (realCount + plusCount < desiredCount && minusCount == 0) {
        // NB: never start replication jobs when removal jobs are in progress, hence the check minusCount > 0.
        if (minusCount > 0) {
            LOG_INFO("Chunk is under-replicated, waiting for pending removals to complete (ChunkId: %s, ReplicaCount: %d+%d-%d, DesiredReplicaCount: %d)",
                ~chunk.Id.ToString(),
                realCount,
                plusCount,
                minusCount,
                desiredCount);
            return;
        }

        auto holderId = GetHolderForReplication(chunk);
        auto& holderInfo = GetHolderInfo(holderId);
        const auto& holder = ChunkManager->GetHolder(holderId);

        holderInfo.ChunksToReplicate.insert(chunk.Id);

        LOG_INFO("Chunk is under-replicated, replication is scheduled at %s (ChunkId: %s, ReplicaCount: %d+%d-%d, DesiredReplicaCount: %d)",
            ~holder.Address,
            ~chunk.Id.ToString(),
            realCount,
            plusCount,
            minusCount,
            desiredCount);
    } else {
        LOG_INFO("Chunk is OK (ChunkId: %s, ReplicaCount: %d+%d-%d, DesiredReplicaCount: %d)",
            ~chunk.Id.ToString(),
            realCount,
            plusCount,
            minusCount,
            desiredCount);
    }
 }

void TChunkReplication::ScheduleRefresh(const TChunkId& chunkId)
{
    if (RefreshSet.find(chunkId) != RefreshSet.end())
        return;

    TRefreshEntry entry;
    entry.ChunkId = chunkId;
    entry.When = TInstant::Now() + ChunkRefreshDelay;
    RefreshList.push_back(entry);
    RefreshSet.insert(chunkId);
}

void TChunkReplication::ScheduleNextRefresh()
{
    TDelayedInvoker::Get()->Submit(
        FromMethod(
            &TChunkReplication::OnRefresh,
            TPtr(this))
        ->Via(Invoker),
        ChunkRefreshQuantum);
}

void TChunkReplication::OnRefresh()
{
    auto now = TInstant::Now();
    for (int i = 0; i < MaxChunksPerRefresh; ++i) {
        if (RefreshList.empty())
            break;

        const auto& entry = RefreshList.front();
        if (entry.When > now)
            break;

        auto* chunk = ChunkManager->FindChunk(entry.ChunkId);
        if (chunk != NULL) {
            Refresh(*chunk);
        }

        YVERIFY(RefreshSet.erase(entry.ChunkId) == 1);
        RefreshList.pop_front();
    }
    ScheduleNextRefresh();
}

void TChunkReplication::StartRefresh( IInvoker::TPtr invoker )
{
    YASSERT(~Invoker == NULL);
    Invoker = invoker;
    ScheduleNextRefresh();
}

void TChunkReplication::StopRefresh()
{
    YASSERT(~Invoker != NULL);
    Invoker.Drop();
}

TChunkReplication::THolderInfo* TChunkReplication::FindHolderInfo(THolderId holderId)
{
    auto it = HolderInfoMap.find(holderId);
    return it == HolderInfoMap.end() ? NULL : &it->Second();
}

TChunkReplication::THolderInfo& TChunkReplication::GetHolderInfo(THolderId holderId)
{
    auto it = HolderInfoMap.find(holderId);
    YASSERT(it != HolderInfoMap.end());
    return it->Second();
}

NYT::NChunkManager::THolderId TChunkReplication::GetHolderForReplication(const TChunk& chunk)
{
    // TODO: pick the least loaded holder
    YASSERT(chunk.Locations.ysize() > 0);
    return chunk.Locations[0];
}

yvector<THolderId> TChunkReplication::GetHoldersForRemoval(const TChunk& chunk, int count)
{
    // TODO: pick the most loaded holder
    yvector<THolderId> result;
    result.reserve(count);
    FOREACH(auto holderId, chunk.Locations) {
        if (result.ysize() >= count)
            break;
        result.push_back(holderId);
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkManager
} // namespace NYT
