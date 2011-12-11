#pragma once

#include "meta_state_manager.h"

#include "../rpc/server.h"
#include "../misc/config.h"

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

//! Describes a configuration of TMetaStateManager.
struct TPersistentStateManagerConfig
    : TConfigBase
{
    //! A path where changelogs are stored.
    Stroka LogPath;

    //! A path where snapshots are stored.
    Stroka SnapshotPath;

    //! Snapshotting period (measured in number of changes).
    /*!
     *  This is also an upper limit for the number of records in a changelog.
     *  
     *  The limit may be violated if the server is under heavy load and
     *  a new snapshot generation request is issued when the previous one is still in progress.
     *  This situation is considered abnormal and a warning is reported.
     *  
     *  A special value of -1 means that snapshot creation is switched off.
     */
    i32 MaxChangesBetweenSnapshots;

    //! Maximum time a follower waits for "Sync" request from the leader.
    TDuration SyncTimeout;

    //! Default timeout for RPC requests.
    TDuration RpcTimeout;

    // TODO: refactor
    TCellConfig Cell;

    TPersistentStateManagerConfig()
    {
        Register("log_path", LogPath).NonEmpty();
        Register("snapshot_path", SnapshotPath).NonEmpty();
        Register("max_changes_between_snapshots", MaxChangesBetweenSnapshots).Default(-1).GreaterThanOrEqual(-1);
        Register("sync_timeout", SyncTimeout).Default(TDuration::MilliSeconds(5000));
        Register("rpc_timeout", RpcTimeout).Default(TDuration::MilliSeconds(3000));
        Register("cell", Cell);

        SetDefaults();
    }
};

////////////////////////////////////////////////////////////////////////////////

IMetaStateManager::TPtr CreatePersistentStateManager(
    const TPersistentStateManagerConfig& config,
    IInvoker* controlInvoker,
    IMetaState* metaState,
    NRpc::IRpcServer* server);

///////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
