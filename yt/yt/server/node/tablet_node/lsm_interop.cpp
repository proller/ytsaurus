#include "lsm_interop.h"

#include "bootstrap.h"
#include "partition_balancer.h"
#include "private.h"
#include "slot_manager.h"
#include "slot_manager.h"
#include "store.h"
#include "store_compactor.h"
#include "store_manager.h"
#include "store_rotator.h"
#include "tablet.h"
#include "tablet_manager.h"
#include "tablet_slot.h"

#include <yt/yt/server/node/cluster_node/config.h>
#include <yt/yt/server/node/cluster_node/dynamic_config_manager.h>

#include <yt/yt/server/lib/cellar_agent/cellar_manager.h>
#include <yt/yt/server/lib/cellar_agent/cellar.h>

#include <yt/yt/server/lib/lsm/partition.h>
#include <yt/yt/server/lib/lsm/store.h>
#include <yt/yt/server/lib/lsm/lsm_backend.h>
#include <yt/yt/server/lib/lsm/tablet.h>

#include <yt/yt/ytlib/tablet_client/config.h>

#include <yt/yt/ytlib/api/native/client.h>
#include <yt/yt/ytlib/api/native/connection.h>

#include <yt/yt/ytlib/misc/memory_usage_tracker.h>

#include <yt/yt/client/transaction_client/timestamp_provider.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NTabletNode {

using namespace NClusterNode;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = TabletNodeLogger;

////////////////////////////////////////////////////////////////////////////////

class TLsmInterop
    : public ILsmInterop
{
public:
    TLsmInterop(
        IBootstrap* bootstrap,
        const IStoreCompactorPtr& storeCompactor,
        const IPartitionBalancerPtr& partitionBalancer,
        const IStoreRotatorPtr& storeRotator)
        : Bootstrap_(bootstrap)
        , StoreCompactor_(storeCompactor)
        , PartitionBalancer_(partitionBalancer)
        , StoreRotator_(storeRotator)
        , Backend_(NLsm::CreateLsmBackend())
    { }

    void Start() override
    {
        const auto& slotManager = Bootstrap_->GetSlotManager();
        slotManager->SubscribeBeginSlotScan(BIND(&TLsmInterop::OnBeginSlotScan, MakeWeak(this)));
        slotManager->SubscribeScanSlot(BIND(&TLsmInterop::OnScanSlot, MakeWeak(this)));
        slotManager->SubscribeEndSlotScan(BIND(&TLsmInterop::OnEndSlotScan, MakeWeak(this)));
    }

private:
    IBootstrap* const Bootstrap_;
    const IStoreCompactorPtr StoreCompactor_;
    const IPartitionBalancerPtr PartitionBalancer_;
    const IStoreRotatorPtr StoreRotator_;
    const NLsm::ILsmBackendPtr Backend_;

    void OnBeginSlotScan()
    {
        YT_LOG_DEBUG("LSM interop begins slot scan");

        StoreCompactor_->OnBeginSlotScan();

        SetBackendState();
    }

    void OnScanSlot(const ITabletSlotPtr& slot)
    {
        if (slot->GetAutomatonState() != NHydra::EPeerState::Leading) {
            return;
        }

        YT_LOG_DEBUG("LSM interop scans slot (CellId: %v)", slot->GetCellId());

        std::vector<NLsm::TTabletPtr> lsmTablets;
        const auto& tabletManager = slot->GetTabletManager();
        for (auto [tabletId, tablet] : tabletManager->Tablets()) {
            lsmTablets.push_back(ScanTablet(slot, tablet));
        }

        YT_LOG_DEBUG("Tablets collected (CellId: %v, TabletCount: %v)",
            slot->GetCellId(),
            lsmTablets.size());

        auto actions = Backend_->BuildLsmActions(lsmTablets, slot->GetTabletCellBundleName());
        StoreCompactor_->ProcessLsmActionBatch(slot, actions);
        PartitionBalancer_->ProcessLsmActionBatch(slot, actions);
        StoreRotator_->ProcessLsmActionBatch(slot, actions);
    }

    void OnEndSlotScan()
    {
        StoreCompactor_->OnEndSlotScan();

        auto actions = Backend_->BuildOverallLsmActions();
        StoreRotator_->ProcessLsmActionBatch(/*slot*/ nullptr, actions);
    }

    void SetBackendState()
    {
        NLsm::TLsmBackendState backendState;

        auto timestampProvider = Bootstrap_
            ->GetMasterClient()
            ->GetNativeConnection()
            ->GetTimestampProvider();
        backendState.CurrentTimestamp = timestampProvider->GetLatestTimestamp();

        backendState.TabletNodeConfig = Bootstrap_->GetConfig()->TabletNode;
        backendState.TabletNodeDynamicConfig = Bootstrap_->GetDynamicConfigManager()->GetConfig()->TabletNode;


        const auto& memoryTracker = Bootstrap_->GetMemoryUsageTracker();
        const auto& cellar = Bootstrap_->GetCellarManager()->GetCellar(NCellarClient::ECellarType::Tablet);
        for (const auto& occupant : cellar->Occupants()) {
            if (!occupant) {
                continue;
            }

            auto occupier = occupant->GetTypedOccupier<ITabletSlot>();
            if (!occupier) {
                continue;
            }

            const auto& bundleName = occupier->GetTabletCellBundleName();
            if (backendState.Bundles.contains(bundleName)) {
                continue;
            }

            const auto& options = occupier->GetDynamicOptions();

            NLsm::TTabletCellBundleState bundleState{
                .ForcedRotationMemoryRatio = options->ForcedRotationMemoryRatio,
                .EnableForcedRotationBackingMemoryAccounting =
                    options->EnableForcedRotationBackingMemoryAccounting,
                .EnablePerBundleMemoryLimit = options->EnableTabletDynamicMemoryLimit,
                .DynamicMemoryLimit =
                    memoryTracker->GetLimit(EMemoryCategory::TabletDynamic, bundleName),
                .DynamicMemoryUsage =
                    memoryTracker->GetUsed(EMemoryCategory::TabletDynamic, bundleName),
            };

            backendState.Bundles[bundleName] = bundleState;
        }

        backendState.DynamicMemoryLimit = memoryTracker->GetLimit(EMemoryCategory::TabletDynamic);
        backendState.DynamicMemoryUsage = memoryTracker->GetUsed(EMemoryCategory::TabletDynamic);

        Backend_->StartNewRound(backendState);
    }

    NLsm::TTabletPtr ScanTablet(const ITabletSlotPtr& slot, TTablet* tablet) const
    {
        const auto& storeManager = tablet->GetStoreManager();

        auto lsmTablet = New<NLsm::TTablet>();
        lsmTablet->SetId(tablet->GetId());
        lsmTablet->SetCellId(slot->GetCellId());
        lsmTablet->TabletCellBundle() = slot->GetTabletCellBundleName();
        lsmTablet->SetPhysicallySorted(tablet->IsPhysicallySorted());
        lsmTablet->SetMounted(tablet->GetState() == ETabletState::Mounted);
        lsmTablet->SetMountConfig(tablet->GetSettings().MountConfig);
        lsmTablet->SetMountRevision(tablet->GetMountRevision());
        lsmTablet->SetStructuredLogger(tablet->GetStructuredLogger());
        lsmTablet->SetLoggingTag(tablet->GetLoggingTag());

        lsmTablet->SetIsRotationPossible(storeManager->IsRotationPossible());
        lsmTablet->SetIsForcedRotationPossible(storeManager->IsForcedRotationPossible());
        lsmTablet->SetIsOverflowRotationNeeded(storeManager->IsOverflowRotationNeeded());
        lsmTablet->SetIsPeriodicRotationNeeded(storeManager->IsPeriodicRotationNeeded());
        lsmTablet->SetPeriodicRotationMilestone(storeManager->GetPeriodicRotationMilestone());

        if (tablet->IsPhysicallySorted()) {
            lsmTablet->Eden() = ScanPartition(tablet->GetEden(), lsmTablet.Get());
            for (const auto& partition : tablet->PartitionList()) {
                lsmTablet->Partitions().push_back(ScanPartition(partition.get(), lsmTablet.Get()));
            }
            lsmTablet->SetOverlappingStoreCount(tablet->GetOverlappingStoreCount());
            lsmTablet->SetEdenOverlappingStoreCount(tablet->GetEdenOverlappingStoreCount());
            lsmTablet->SetCriticalPartitionCount(tablet->GetCriticalPartitionCount());
        } else {
            for (const auto& [id, store] : tablet->StoreIdMap()) {
                lsmTablet->Stores().push_back(ScanStore(store, lsmTablet.Get()));
            }
        }

        return lsmTablet;
    }

    std::unique_ptr<NLsm::TPartition> ScanPartition(TPartition* partition, NLsm::TTablet* lsmTablet) const
    {
        auto lsmPartition = std::make_unique<NLsm::TPartition>();
        lsmPartition->SetTablet(lsmTablet);
        lsmPartition->SetId(partition->GetId());
        lsmPartition->SetIndex(partition->GetIndex());
        lsmPartition->PivotKey() = partition->GetPivotKey();
        lsmPartition->NextPivotKey() = partition->GetNextPivotKey();
        lsmPartition->SetState(partition->GetState());
        lsmPartition->SetCompactionTime(partition->GetCompactionTime());
        lsmPartition->SetAllowedSplitTime(partition->GetAllowedSplitTime());
        lsmPartition->SetSamplingRequestTime(partition->GetSamplingRequestTime());
        lsmPartition->SetSamplingTime(partition->GetSamplingTime());
        lsmPartition->SetIsImmediateSplitRequested(partition->IsImmediateSplitRequested());
        lsmPartition->SetCompressedDataSize(partition->GetCompressedDataSize());
        lsmPartition->SetUncompressedDataSize(partition->GetUncompressedDataSize());

        for (const auto& store : partition->Stores()) {
            lsmPartition->Stores().push_back(ScanStore(store, lsmTablet));
        }

        return lsmPartition;
    }

    std::unique_ptr<NLsm::TStore> ScanStore(const IStorePtr& store, NLsm::TTablet* lsmTablet) const
    {
        const auto& tablet = store->GetTablet();
        const auto& storeManager = tablet->GetStoreManager();

        auto lsmStore = std::make_unique<NLsm::TStore>();
        lsmStore->SetTablet(lsmTablet);
        lsmStore->SetId(store->GetId());
        lsmStore->SetType(store->GetType());
        lsmStore->SetStoreState(store->GetStoreState());
        lsmStore->SetCompressedDataSize(store->GetCompressedDataSize());
        lsmStore->SetUncompressedDataSize(store->GetUncompressedDataSize());
        lsmStore->SetRowCount(store->GetRowCount());
        lsmStore->SetMinTimestamp(store->GetMinTimestamp());
        lsmStore->SetMaxTimestamp(store->GetMaxTimestamp());

        if (store->IsDynamic()) {
            auto dynamicStore = store->AsDynamic();
            lsmStore->SetFlushState(dynamicStore->GetFlushState());
            lsmStore->SetLastFlushAttemptTimestamp(
                dynamicStore->GetLastFlushAttemptTimestamp());
            lsmStore->SetDynamicMemoryUsage(dynamicStore->GetDynamicMemoryUsage());
        }

        if (store->IsChunk()) {
            auto chunkStore = store->AsChunk();
            lsmStore->SetPreloadState(chunkStore->GetPreloadState());
            lsmStore->SetCompactionState(chunkStore->GetCompactionState());
            lsmStore->SetIsCompactable(storeManager->IsStoreCompactable(store));
            lsmStore->SetCreationTime(chunkStore->GetCreationTime());
            lsmStore->SetLastCompactionTimestamp(chunkStore->GetLastCompactionTimestamp());

            if (auto backingStore = chunkStore->GetBackingStore()) {
                lsmStore->SetBackingStoreMemoryUsage(backingStore->GetDynamicMemoryUsage());
            }
        }

        if (store->IsSorted()) {
            auto sortedStore = store->AsSorted();
            lsmStore->MinKey() = sortedStore->GetMinKey();
            lsmStore->UpperBoundKey() = sortedStore->GetUpperBoundKey();
        }

        return lsmStore;
    }
};

////////////////////////////////////////////////////////////////////////////////

ILsmInteropPtr CreateLsmInterop(
    IBootstrap* bootstrap,
    const IStoreCompactorPtr& storeCompactor,
    const IPartitionBalancerPtr& partitionBalancer,
    const IStoreRotatorPtr& storeRotator)
{
    return New<TLsmInterop>(bootstrap, storeCompactor, partitionBalancer, storeRotator);
}

////////////////////////////////////////////////////////////////////////////////

} // NYT::NTabletNode
