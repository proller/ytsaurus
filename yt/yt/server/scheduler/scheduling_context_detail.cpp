#include "scheduling_context_detail.h"

#include "exec_node.h"
#include "allocation.h"
#include "private.h"

#include <yt/yt/server/lib/scheduler/config.h>
#include <yt/yt/server/lib/scheduler/structs.h>

#include <yt/yt/ytlib/scheduler/disk_resources.h>
#include <yt/yt/ytlib/scheduler/job_resources_helpers.h>

#include <yt/yt/client/node_tracker_client/helpers.h>

#include <yt/yt/client/object_client/helpers.h>

namespace NYT::NScheduler {

using namespace NObjectClient;
using namespace NControllerAgent;

using NYT::FromProto;

////////////////////////////////////////////////////////////////////////////////

TSchedulingContextBase::TSchedulingContextBase(
    int nodeShardId,
    TSchedulerConfigPtr config,
    TExecNodePtr node,
    const std::vector<TJobPtr>& runningJobs,
    const NChunkClient::TMediumDirectoryPtr& mediumDirectory)
    : NodeShardId_(nodeShardId)
    , Config_(std::move(config))
    , Node_(std::move(node))
    , NodeDescriptor_(Node_->BuildExecDescriptor())
    , NodeTags_(Node_->Tags())
    , MediumDirectory_(mediumDirectory)
    , DefaultMinSpareJobResources_(
        Config_->MinSpareJobResourcesOnNode
        ? ToJobResources(*Config_->MinSpareJobResourcesOnNode, TJobResources())
        : TJobResources())
    , ResourceUsage_(Node_->GetResourceUsage())
    , ResourceLimits_(Node_->GetResourceLimits())
    , DiskResources_(Node_->GetDiskResources())
    , RunningJobs_(runningJobs)
{
    if (const auto& diskLocationResources = DiskResources_.DiskLocationResources;
        diskLocationResources.size() == 1 &&
        Config_->ConsiderDiskQuotaInPreemptiveSchedulingDiscount)
    {
        DiscountMediumIndex_ = diskLocationResources.front().MediumIndex;
    }
}

int TSchedulingContextBase::GetNodeShardId() const
{
    return NodeShardId_;
}

TJobResources& TSchedulingContextBase::ResourceUsage()
{
    return ResourceUsage_;
}

const TJobResources& TSchedulingContextBase::ResourceUsage() const
{
    return ResourceUsage_;
}

const TJobResources& TSchedulingContextBase::ResourceLimits() const
{
    return ResourceLimits_;
}

const TJobResourcesWithQuota& TSchedulingContextBase::UnconditionalDiscount() const
{
    return UnconditionalDiscount_;
}

TJobResourcesWithQuota TSchedulingContextBase::GetConditionalDiscountForOperation(TOperationId operationId) const
{
    return GetOrDefault(ConditionalDiscountMap_, operationId);
}

TJobResourcesWithQuota TSchedulingContextBase::GetMaxConditionalDiscount() const
{
    return MaxConditionalDiscount_;
}

void TSchedulingContextBase::IncreaseUnconditionalDiscount(const TJobResourcesWithQuota& jobResources)
{
    UnconditionalDiscount_.SetJobResources(UnconditionalDiscount_.ToJobResources() + jobResources.ToJobResources());

    if (DiscountMediumIndex_) {
        auto compactedDiskQuota = GetDiskQuotaWithCompactedDefaultMedium(jobResources.DiskQuota());
        UnconditionalDiscount_.DiskQuota().DiskSpacePerMedium[*DiscountMediumIndex_] +=
            GetOrDefault(compactedDiskQuota.DiskSpacePerMedium, *DiscountMediumIndex_);
    }
}

const TDiskResources& TSchedulingContextBase::DiskResources() const
{
    return DiskResources_;
}

TDiskResources& TSchedulingContextBase::DiskResources()
{
    return DiskResources_;
}

const std::vector<TDiskQuota>& TSchedulingContextBase::DiskRequests() const
{
    return DiskRequests_;
}

const TExecNodeDescriptorPtr& TSchedulingContextBase::GetNodeDescriptor() const
{
    return NodeDescriptor_;
}

bool TSchedulingContextBase::CanSatisfyResourceRequest(
    const TJobResources& jobResources,
    const TJobResources& conditionalDiscount) const
{
    return Dominates(
        ResourceLimits_,
        ResourceUsage_ + jobResources - (UnconditionalDiscount_.ToJobResources() + conditionalDiscount));
}

bool TSchedulingContextBase::CanStartJobForOperation(
    const TJobResourcesWithQuota& jobResourcesWithQuota,
    TOperationId operationId) const
{
    std::vector<NScheduler::TDiskQuota> diskRequests(DiskRequests_);
    auto diskRequest = Max(TDiskQuota{},
        GetDiskQuotaWithCompactedDefaultMedium(jobResourcesWithQuota.DiskQuota()) - (UnconditionalDiscount_.DiskQuota() + GetConditionalDiscountForOperation(operationId).DiskQuota()));
    diskRequests.push_back(std::move(diskRequest));

    return
        CanSatisfyResourceRequest(
            jobResourcesWithQuota.ToJobResources(),
            GetConditionalDiscountForOperation(operationId).ToJobResources()) &&
        CanSatisfyDiskQuotaRequests(DiskResources_, diskRequests);
}

bool TSchedulingContextBase::CanStartMoreJobs(
    const std::optional<TJobResources>& customMinSpareJobResources) const
{
    auto minSpareJobResources = customMinSpareJobResources.value_or(DefaultMinSpareJobResources_);
    if (!CanSatisfyResourceRequest(minSpareJobResources, MaxConditionalDiscount_.ToJobResources())) {
        return false;
    }

    auto limit = Config_->MaxStartedJobsPerHeartbeat;
    return !limit || std::ssize(StartedJobs_) < *limit;
}

bool TSchedulingContextBase::CanSchedule(const TSchedulingTagFilter& filter) const
{
    return filter.IsEmpty() || filter.CanSchedule(NodeTags_);
}

bool TSchedulingContextBase::ShouldAbortJobsSinceResourcesOvercommit() const
{
    bool resourcesOvercommitted = !Dominates(ResourceLimits(), ResourceUsage());
    auto now = NProfiling::CpuInstantToInstant(GetNow());
    bool allowedOvercommitTimePassed = Node_->GetResourcesOvercommitStartTime()
        ? Node_->GetResourcesOvercommitStartTime() + Config_->AllowedNodeResourcesOvercommitDuration < now
        : false;
    return resourcesOvercommitted && allowedOvercommitTimePassed;
}

const std::vector<TJobPtr>& TSchedulingContextBase::StartedJobs() const
{
    return StartedJobs_;
}

const std::vector<TJobPtr>& TSchedulingContextBase::RunningJobs() const
{
    return RunningJobs_;
}

const std::vector<TPreemptedJob>& TSchedulingContextBase::PreemptedJobs() const
{
    return PreemptedJobs_;
}

void TSchedulingContextBase::StartJob(
    const TString& treeId,
    TOperationId operationId,
    TIncarnationId incarnationId,
    TControllerEpoch controllerEpoch,
    const TJobStartDescriptor& startDescriptor,
    EPreemptionMode preemptionMode,
    int schedulingIndex,
    EJobSchedulingStage schedulingStage)
{
    ResourceUsage_ += startDescriptor.ResourceLimits.ToJobResources();
    if (startDescriptor.ResourceLimits.DiskQuota()) {
        DiskRequests_.push_back(startDescriptor.ResourceLimits.DiskQuota());
    }
    auto startTime = NProfiling::CpuInstantToInstant(GetNow());
    auto job = New<TJob>(
        startDescriptor.Id,
        operationId,
        incarnationId,
        controllerEpoch,
        Node_,
        startTime,
        startDescriptor.ResourceLimits.ToJobResources(),
        startDescriptor.ResourceLimits.DiskQuota(),
        startDescriptor.Interruptible,
        preemptionMode,
        treeId,
        schedulingIndex,
        schedulingStage);
    StartedJobs_.push_back(job);
}

void TSchedulingContextBase::PreemptJob(const TJobPtr& job, TDuration interruptTimeout, EJobPreemptionReason preemptionReason)
{
    YT_VERIFY(job->GetNode() == Node_);
    PreemptedJobs_.push_back({job, interruptTimeout, preemptionReason});

    if (auto it = DiskRequestIndexPerJobId_.find(job->GetId());
        it != DiskRequestIndexPerJobId_.end() && DiscountMediumIndex_)
    {
        DiskRequests_[it->second] = TDiskQuota{};
    }
}

TJobResources TSchedulingContextBase::GetNodeFreeResourcesWithoutDiscount() const
{
    return ResourceLimits_ - ResourceUsage_;
}

TJobResources TSchedulingContextBase::GetNodeFreeResourcesWithDiscount() const
{
    return ResourceLimits_ - ResourceUsage_ + UnconditionalDiscount_.ToJobResources();
}

TJobResources TSchedulingContextBase::GetNodeFreeResourcesWithDiscountForOperation(TOperationId operationId) const
{
    return ResourceLimits_ - ResourceUsage_ + UnconditionalDiscount_.ToJobResources() + GetConditionalDiscountForOperation(operationId).ToJobResources();
}

TDiskResources TSchedulingContextBase::GetDiskResourcesWithDiscountForOperation(TOperationId operationId) const
{
    auto diskResources = DiskResources_;
    if (DiscountMediumIndex_) {
        auto discountForOperation = GetOrDefault(UnconditionalDiscount_.DiskQuota().DiskSpacePerMedium, *DiscountMediumIndex_) +
            GetOrDefault(GetConditionalDiscountForOperation(operationId).DiskQuota().DiskSpacePerMedium, *DiscountMediumIndex_);

        auto& diskLocation = diskResources.DiskLocationResources.front();
        diskLocation.Usage = std::max(0l, diskLocation.Usage - discountForOperation);
    }
    return diskResources;
}

TScheduleJobsStatistics TSchedulingContextBase::GetSchedulingStatistics() const
{
    return SchedulingStatistics_;
}

void TSchedulingContextBase::SetSchedulingStatistics(TScheduleJobsStatistics statistics)
{
    SchedulingStatistics_ = statistics;
}

void TSchedulingContextBase::StoreScheduleJobExecDurationEstimate(TDuration duration)
{
    YT_ASSERT(!ScheduleJobExecDurationEstimate_);

    ScheduleJobExecDurationEstimate_ = duration;
}

TDuration TSchedulingContextBase::ExtractScheduleJobExecDurationEstimate()
{
    YT_ASSERT(ScheduleJobExecDurationEstimate_);

    return *std::exchange(ScheduleJobExecDurationEstimate_, {});
}

ENodeSchedulingResult TSchedulingContextBase::GetNodeSchedulingResult() const
{
    return NodeSchedulingResult_;
}

void TSchedulingContextBase::SetNodeSchedulingResult(ENodeSchedulingResult result)
{
    NodeSchedulingResult_ = result;
}

void TSchedulingContextBase::ResetDiscounts()
{
    UnconditionalDiscount_ = {};
    ConditionalDiscountMap_.clear();
    MaxConditionalDiscount_ = {};
}

void TSchedulingContextBase::SetConditionalDiscountForOperation(TOperationId operationId, const TJobResourcesWithQuota& discountForOperation)
{
    TJobResourcesWithQuota conditionalDiscount(discountForOperation.ToJobResources());

    if (DiscountMediumIndex_) {
        auto compactedDiscount = GetDiskQuotaWithCompactedDefaultMedium(discountForOperation.DiskQuota());
        conditionalDiscount.DiskQuota().DiskSpacePerMedium[*DiscountMediumIndex_] =
            GetOrDefault(compactedDiscount.DiskSpacePerMedium, *DiscountMediumIndex_);
    }

    EmplaceOrCrash(ConditionalDiscountMap_, operationId, conditionalDiscount);
    MaxConditionalDiscount_ = Max(MaxConditionalDiscount_, conditionalDiscount);
}

TDiskQuota TSchedulingContextBase::GetDiskQuotaWithCompactedDefaultMedium(TDiskQuota diskQuota) const
{
    if (diskQuota.DiskSpaceWithoutMedium) {
        diskQuota.DiskSpacePerMedium[DiskResources_.DefaultMediumIndex] += *diskQuota.DiskSpaceWithoutMedium;
        diskQuota.DiskSpaceWithoutMedium = {};
    }

    return diskQuota;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
