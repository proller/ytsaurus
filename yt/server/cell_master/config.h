#pragma once

#include "public.h"

#include <ytlib/election/config.h>

#include <ytlib/hive/config.h>

#include <server/hydra/config.h>

#include <server/hive/config.h>

#include <server/node_tracker_server/config.h>

#include <server/transaction_server/config.h>

#include <server/chunk_server/config.h>

#include <server/cypress_server/config.h>

#include <server/object_server/config.h>

#include <server/security_server/config.h>

#include <server/tablet_server/config.h>

#include <server/misc/config.h>

namespace NYT {
namespace NCellMaster {

////////////////////////////////////////////////////////////////////////////////

class TMasterCellConfig
    : public NElection::TCellConfig
{
public:
    ui16 CellId;

    TMasterCellConfig()
    {
        RegisterParameter("cell_id", CellId)
            .Default(0);
    }
};

class TCellMasterConfig
    : public TServerConfig
{
public:
    TMasterCellConfigPtr Masters;

    NHydra::TFileChangelogStoreConfigPtr Changelogs;
    NHydra::TFileSnapshotStoreConfigPtr Snapshots;
    NHydra::TDistributedHydraManagerConfigPtr HydraManager;

    NHive::TCellDirectoryConfigPtr CellDirectory;
    NHive::THiveManagerConfigPtr HiveManager;

    NNodeTrackerServer::TNodeTrackerConfigPtr NodeTracker;

    NTransactionServer::TTransactionManagerConfigPtr TransactionManager;

    NChunkServer::TChunkManagerConfigPtr ChunkManager;

    NObjectServer::TObjectManagerConfigPtr ObjectManager;

    NCypressServer::TCypressManagerConfigPtr CypressManager;

    NSecurityServer::TSecurityManagerConfigPtr SecurityManager;

    NTabletServer::TTabletManagerConfigPtr TabletManager;

    NHive::TTimestampManagerConfigPtr TimestampManager;

    NHive::TRemoteTimestampProviderConfigPtr TimestampProvider;

    NHive::TTransactionSupervisorConfigPtr TransactionSupervisor;

    //! RPC interface port number.
    int RpcPort;

    //! HTTP monitoring interface port number.
    int MonitoringPort;

    TCellMasterConfig()
    {
        RegisterParameter("masters", Masters);
        RegisterParameter("changelogs", Changelogs);
        RegisterParameter("snapshots", Snapshots);
        RegisterParameter("hydra_manager", HydraManager)
            .DefaultNew();
        RegisterParameter("cell_directory", CellDirectory)
            .DefaultNew();
        RegisterParameter("hive_manager", HiveManager)
            .DefaultNew();
        RegisterParameter("node_tracker", NodeTracker)
            .DefaultNew();
        RegisterParameter("transaction_manager", TransactionManager)
            .DefaultNew();
        RegisterParameter("chunk_manager", ChunkManager)
            .DefaultNew();
        RegisterParameter("object_manager", ObjectManager)
            .DefaultNew();
        RegisterParameter("cypress_manager", CypressManager)
            .DefaultNew();
        RegisterParameter("security_manager", SecurityManager)
            .DefaultNew();
        RegisterParameter("tablet_manager", TabletManager)
            .DefaultNew();
        RegisterParameter("timestamp_manager", TimestampManager)
            .DefaultNew();
        RegisterParameter("timestamp_provider", TimestampProvider);
        RegisterParameter("transaction_supervisor", TransactionSupervisor)
            .DefaultNew();
        RegisterParameter("rpc_port", RpcPort)
            .Default(9000);
        RegisterParameter("monitoring_port", MonitoringPort)
            .Default(10000);
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellMaster
} // namespace NYT
