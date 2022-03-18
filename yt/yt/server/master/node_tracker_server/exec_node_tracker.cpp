#include "exec_node_tracker.h"

#include "private.h"
#include "node.h"
#include "node_tracker.h"

#include <yt/yt/server/master/cell_master/automaton.h>
#include <yt/yt/server/master/cell_master/bootstrap.h>
#include <yt/yt/server/master/cell_master/config.h>
#include <yt/yt/server/master/cell_master/config_manager.h>
#include <yt/yt/server/master/cell_master/hydra_facade.h>

#include <yt/yt/server/master/node_tracker_server/config.h>

#include <yt/yt/ytlib/node_tracker_client/helpers.h>

namespace NYT::NNodeTrackerServer {

using namespace NCellMaster;
using namespace NConcurrency;
using namespace NExecNodeTrackerClient::NProto;
using namespace NHydra;
using namespace NNodeTrackerClient;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = NodeTrackerServerLogger;

////////////////////////////////////////////////////////////////////////////////

class TExecNodeTracker
    : public IExecNodeTracker
    , public TMasterAutomatonPart
{
public:
    explicit TExecNodeTracker(TBootstrap* bootstrap)
        : TMasterAutomatonPart(bootstrap, EAutomatonThreadQueue::ExecNodeTracker)
    {
        RegisterMethod(BIND(&TExecNodeTracker::HydraExecNodeHeartbeat, Unretained(this)));
    }

    void Initialize() override
    {
        const auto& configManager = Bootstrap_->GetConfigManager();
        configManager->SubscribeConfigChanged(BIND(&TExecNodeTracker::OnDynamicConfigChanged, MakeWeak(this)));
    }

    void ProcessHeartbeat(TCtxHeartbeatPtr context) override
    {
        auto mutation = CreateMutation(
            Bootstrap_->GetHydraFacade()->GetHydraManager(),
            context,
            &TExecNodeTracker::HydraExecNodeHeartbeat,
            this);
        CommitMutationWithSemaphore(std::move(mutation), std::move(context), HeartbeatSemaphore_);
    }

    void ProcessHeartbeat(
        TNode* node,
        TReqHeartbeat* request,
        TRspHeartbeat* response) override
    {
        const auto& multicellManager = Bootstrap_->GetMulticellManager();
        YT_VERIFY(multicellManager->IsPrimaryMaster());

        YT_VERIFY(node->IsExecNode());

        auto& statistics = *request->mutable_statistics();
        node->SetExecNodeStatistics(std::move(statistics));

        const auto& nodeTracker = Bootstrap_->GetNodeTracker();
        nodeTracker->OnNodeHeartbeat(node, ENodeHeartbeatType::Exec);

        response->set_disable_scheduler_jobs(node->GetDisableSchedulerJobs());
    }

private:
    const TAsyncSemaphorePtr HeartbeatSemaphore_ = New<TAsyncSemaphore>(0);

    void HydraExecNodeHeartbeat(
        const TCtxHeartbeatPtr& /*context*/,
        TReqHeartbeat* request,
        TRspHeartbeat* response)
    {
        auto nodeId = request->node_id();
        auto& statistics = *request->mutable_statistics();

        const auto& nodeTracker = Bootstrap_->GetNodeTracker();
        auto* node = nodeTracker->GetNodeOrThrow(nodeId);

        node->ValidateRegistered();

        YT_PROFILE_TIMING("/node_tracker/exec_node_heartbeat_time") {
            YT_LOG_DEBUG_IF(IsMutationLoggingEnabled(), "Processing exec node heartbeat (NodeId: %v, Address: %v, State: %v, %v",
                nodeId,
                node->GetDefaultAddress(),
                node->GetLocalState(),
                statistics);

            nodeTracker->UpdateLastSeenTime(node);

            ProcessHeartbeat(node, request, response);
        }
    }

    void CommitMutationWithSemaphore(
        std::unique_ptr<TMutation> mutation,
        NRpc::IServiceContextPtr context,
        const TAsyncSemaphorePtr& semaphore)
    {
        auto timeBefore = NProfiling::GetInstant();
        auto handler = BIND([mutation = std::move(mutation), context = std::move(context), timeBefore] (TAsyncSemaphoreGuard) {
            auto requestTimeout = context->GetTimeout();
            auto timeAfter = NProfiling::GetInstant();
            if (requestTimeout && timeAfter >= timeBefore + *requestTimeout) {
                context->Reply(TError(NYT::EErrorCode::Timeout, "Semaphore acquisition took longer than request timeout"));
            } else {
                Y_UNUSED(WaitFor(mutation->CommitAndReply(context)));
            }
        });

        semaphore->AsyncAcquire(handler, EpochAutomatonInvoker_);
    }

    const TDynamicNodeTrackerConfigPtr& GetDynamicConfig() const
    {
        return Bootstrap_->GetConfigManager()->GetConfig()->NodeTracker;
    }

    void OnDynamicConfigChanged(TDynamicClusterConfigPtr /*oldConfig*/ = nullptr)
    {
        HeartbeatSemaphore_->SetTotal(GetDynamicConfig()->MaxConcurrentExecNodeHeartbeats);
    }
};

////////////////////////////////////////////////////////////////////////////////

IExecNodeTrackerPtr CreateExecNodeTracker(NCellMaster::TBootstrap* bootstrap)
{
    return New<TExecNodeTracker>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNodeTrackerServer
