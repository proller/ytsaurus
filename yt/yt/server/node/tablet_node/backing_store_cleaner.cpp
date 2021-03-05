#include "backing_store_cleaner.h"

#include "private.h"
#include "slot_manager.h"
#include "store.h"
#include "tablet.h"
#include "tablet_manager.h"
#include "tablet_slot.h"

#include <yt/yt/server/node/cluster_node/bootstrap.h>

#include <yt/yt/ytlib/misc/memory_usage_tracker.h>

#include <yt/yt/ytlib/tablet_client/config.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NTabletNode {

using namespace NClusterNode;
using namespace NNodeTrackerClient;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = TabletNodeLogger;
static const auto& Profiler = TabletNodeProfiler;

////////////////////////////////////////////////////////////////////////////////

/*!
 * Backing store cleaner operates on bundles with max_backing_store_memory_ratio set
 * and forcefully releases old backing stores if they occupy too much memory.
 * Stores of each bundle are released in ascending order by creation time.
 */
class TBackingStoreCleaner
    : public IBackingStoreCleaner
{
public:
    explicit TBackingStoreCleaner(TBootstrap* bootstrap)
        : Bootstrap_(bootstrap)
    { }

    virtual void Start() override
    {
        const auto& slotManager = Bootstrap_->GetTabletSlotManager();
        slotManager->SubscribeBeginSlotScan(BIND(&TBackingStoreCleaner::OnBeginSlotScan, MakeStrong(this)));
        slotManager->SubscribeScanSlot(BIND(&TBackingStoreCleaner::OnScanSlot, MakeStrong(this)));
        slotManager->SubscribeEndSlotScan(BIND(&TBackingStoreCleaner::OnEndSlotScan, MakeStrong(this)));
    }

private:
    TBootstrap* const Bootstrap_;

    YT_DECLARE_SPINLOCK(TAdaptiveLock, SpinLock_);

    struct TCounters
    {
        explicit TCounters(const TString& bundleName)
            : RetentionTime(Profiler
                .WithTag("tablet_cell_bundle", bundleName)
                .TimeGauge("/backing_store_retention_time"))
        { }

        NProfiling::TTimeGauge RetentionTime;
    };

    THashMap<TString, TCounters> Counters_;

    TCounters* GetCounters(const TString& bundleName)
    {
        if (auto it = Counters_.find(bundleName); it != Counters_.end()) {
            return &it->second;
        }
        auto [it, inserted] = Counters_.emplace(bundleName, bundleName);
        return &it->second;
    }

    struct TStoreData
    {
        IChunkStorePtr Store;
        TInstant CreationTime;
        i64 BackingStoreSize = 0;
        TTabletSlotPtr Slot;
    };

    struct TTabletCellBundleData
    {
        i64 MemoryLimit = 0;
        i64 MemoryUsage = 0;
        std::vector<TStoreData> Stores;
    };

    THashMap<TString, TTabletCellBundleData> NameToBundleData_;

    void EnsureBundleDataCreated(const TTabletSlotPtr& slot)
    {
        VERIFY_SPINLOCK_AFFINITY(SpinLock_);

        if (NameToBundleData_.contains(slot->GetTabletCellBundleName())) {
            return;
        }

        i64 memoryLimit;

        const auto& dynamicOptions = slot->GetDynamicOptions();

        if (dynamicOptions->MaxBackingStoreMemoryRatio) {
            const auto& memoryTracker = Bootstrap_->GetMemoryUsageTracker();
            auto poolTag = dynamicOptions->EnableTabletDynamicMemoryLimit
                ? std::make_optional(slot->GetTabletCellBundleName())
                : std::nullopt;
            memoryLimit = memoryTracker->GetFree(EMemoryCategory::TabletDynamic, poolTag) *
                *dynamicOptions->MaxBackingStoreMemoryRatio;
        } else {
            memoryLimit = std::numeric_limits<i64>::max() / 2;
        }

        NameToBundleData_.emplace(
            slot->GetTabletCellBundleName(),
            TTabletCellBundleData{memoryLimit, 0, {}});
    }

    void OnBeginSlotScan()
    {
        NameToBundleData_.clear();
    }

    void OnScanSlot(const TTabletSlotPtr& slot)
    {
        {
            auto guard = Guard(SpinLock_);
            EnsureBundleDataCreated(slot);
        }

        const auto& tabletManager = slot->GetTabletManager();
        for (auto [tabletId, tablet] : tabletManager->Tablets()) {
            ScanTablet(slot, tablet);
        }
    }

    void ScanTablet(const TTabletSlotPtr& slot, TTablet* tablet)
    {
        std::vector<TStoreData> stores;
        i64 memoryUsage = 0;

        for (const auto& [storeId, store] : tablet->StoreIdMap()) {
            if (store->GetStoreState() != EStoreState::Persistent) {
                continue;
            }
            auto chunkStore = store->AsChunk();
            if (auto backingStore = chunkStore->GetBackingStore()) {
                stores.push_back({
                    .Store = chunkStore,
                    .CreationTime = chunkStore->GetCreationTime(),
                    .BackingStoreSize = backingStore->GetDynamicMemoryUsage(),
                    .Slot = slot
                });
                memoryUsage += backingStore->GetDynamicMemoryUsage();
            }
        }

        if (!stores.empty()) {
            auto guard = Guard(SpinLock_);

            auto& bundleData = NameToBundleData_[slot->GetTabletCellBundleName()];
            bundleData.Stores.insert(bundleData.Stores.end(), stores.begin(), stores.end());
            bundleData.MemoryUsage += memoryUsage;
        }
    }

    void OnEndSlotScan()
    {
        THashMap<TTabletSlotPtr, std::vector<IChunkStorePtr>> slotToStoresToRelease;

        auto now = TInstant::Now();

        for (auto& [bundleName, bundleData] : NameToBundleData_) {
            auto& stores = bundleData.Stores;
            std::sort(
                stores.begin(),
                stores.end(),
                [] (const TStoreData& lhs, const TStoreData& rhs) {
                    return lhs.CreationTime < rhs.CreationTime;
                });

            i64 memoryOvercommit = bundleData.MemoryUsage - bundleData.MemoryLimit;

            if (memoryOvercommit <= 0) {
                auto* counters = GetCounters(bundleName);
                if (stores.empty()) {
                    counters->RetentionTime.Update(TDuration::Zero());
                } else {
                    counters->RetentionTime.Update(now - stores[0].CreationTime);
                }
                continue;
            }

            YT_LOG_DEBUG("Backing memory limit exceeded "
                "(TabletCellBundle: %v, MemoryLimit: %v, MemoryUsage: %v, Overcommit: %v)",
                bundleName,
                bundleData.MemoryLimit,
                bundleData.MemoryUsage,
                memoryOvercommit);

            int storeIndex = 0;
            for (; storeIndex < stores.size(); ++storeIndex) {
                const auto& storeData = stores[storeIndex];
                slotToStoresToRelease[storeData.Slot].push_back(storeData.Store);
                memoryOvercommit -= storeData.BackingStoreSize;

                if (memoryOvercommit <= 0) {
                    ++storeIndex;
                    break;
                }
            }

            auto retentionTime = storeIndex == static_cast<int>(stores.size())
                ? TDuration::Zero()
                : now - stores[storeIndex].CreationTime;
            GetCounters(bundleName)->RetentionTime.Update(retentionTime);
        }

        for (const auto& [slot, stores] : slotToStoresToRelease) {
            auto invoker = slot->GetAutomatonInvoker();
            // NB: cannot capture structured binding element in lambda.
            invoker->Invoke(BIND([slot = slot, stores = stores] {
                const auto& tabletManager = slot->GetTabletManager();
                for (const auto& store : stores) {
                    tabletManager->ReleaseBackingStore(store);
                }
            }));
        }
    }
};

IBackingStoreCleanerPtr CreateBackingStoreCleaner(NClusterNode::TBootstrap* bootstrap)
{
    return New<TBackingStoreCleaner>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
