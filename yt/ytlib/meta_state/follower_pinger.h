#pragma once

#include "public.h"
#include "meta_state_manager_proxy.h"

#include <ytlib/misc/periodic_invoker.h>
#include <ytlib/misc/configurable.h>
#include <ytlib/misc/thread_affinity.h>

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

class TFollowerPinger
    : public TRefCounted
{
public:
    TFollowerPinger(
        TFollowerPingerConfigPtr config,
        TCellManagerPtr cellManager,
        TDecoratedMetaStatePtr decoratedState,
        TFollowerTrackerPtr followerTracker,
        const TEpoch& epoch,
        IInvoker::TPtr epochControlInvoker);

    void Start();
    void Stop();

private:
    typedef TMetaStateManagerProxy TProxy;

    void SendPing(TPeerId followerId);
    void SchedulePing(TPeerId followerId);
    void OnPingResponse(TPeerId followerId, TProxy::TRspPingFollower::TPtr response);

    TFollowerPingerConfigPtr Config;
    TPeriodicInvoker::TPtr PeriodicInvoker;
    TCellManagerPtr CellManager;
    TDecoratedMetaStatePtr DecoratedState;
    TFollowerTrackerPtr FollowerTracker;
    TSnapshotStorePtr SnapshotStore;
    TEpoch Epoch;
    IInvoker::TPtr EpochControlInvoker;

    DECLARE_THREAD_AFFINITY_SLOT(ControlThread);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
