#include "config.h"

namespace NYT::NTabletClient {

////////////////////////////////////////////////////////////////////////////////

TTabletCellOptions::TTabletCellOptions()
{
    RegisterParameter("peer_count", PeerCount)
        .Default(1)
        .InRange(1, MaxPeerCount);
    RegisterParameter("independent_peers", IndependentPeers)
        .Default(false);
}

////////////////////////////////////////////////////////////////////////////////

TDynamicTabletCellOptions::TDynamicTabletCellOptions()
{
    RegisterParameter("cpu_per_tablet_slot", CpuPerTabletSlot)
        .Optional();
    RegisterParameter("suppress_tablet_cell_decommission", SuppressTabletCellDecommission)
        .Optional();
    RegisterParameter("forced_rotation_memory_ratio", ForcedRotationMemoryRatio)
        .InRange(0.0, 1.0)
        .Default(0.8);
    RegisterParameter("enable_forced_rotation_backing_memory_accounting", EnableForcedRotationBackingMemoryAccounting)
        .Default(true);
    RegisterParameter("dynamic_memory_pool_weight", DynamicMemoryPoolWeight)
        .InRange(1, MaxDynamicMemoryPoolWeight)
        .Default(1);
    RegisterParameter("enable_tablet_dynamic_memory_limit", EnableTabletDynamicMemoryLimit)
        .Default(true);
    RegisterParameter("solomon_tag", SolomonTag)
        .Optional()
        .DontSerializeDefault();
    RegisterParameter("max_backing_store_memory_ratio", MaxBackingStoreMemoryRatio)
        .Default();
    RegisterParameter("increase_upload_replication_factor", IncreaseUploadReplicationFactor)
        .Default(false);

    RegisterPostprocessor([&] {
        if (!EnableForcedRotationBackingMemoryAccounting &&
            MaxBackingStoreMemoryRatio &&
            *MaxBackingStoreMemoryRatio + ForcedRotationMemoryRatio >= 1.0)
        {
            THROW_ERROR_EXCEPTION("\"max_backing_store_memory_ratio\" + "
                "\"forced_rotation_memory_ratio\" should be less than 1"
                " if \"enable_forced_rotation_backing_memory_accounting\" is false");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletClient
