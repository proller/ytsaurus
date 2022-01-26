#include "private.h"
#include "snapshot_discovery.h"
#include "snapshot_download.h"
#include "file_snapshot_store.h"
#include "local_snapshot_store.h"

#include <yt/yt/server/lib/hydra_common/config.h>
#include <yt/yt/server/lib/hydra_common/snapshot.h>
#include <yt/yt/server/lib/hydra_common/private.h>

#include <yt/yt/ytlib/election/cell_manager.h>

#include <yt/yt/core/concurrency/scheduler.h>

namespace NYT::NHydra {

using namespace NElection;
using namespace NConcurrency;
using namespace NHydra::NProto;

////////////////////////////////////////////////////////////////////////////////

class TLocalSnapshotReader
    : public ISnapshotReader
{
public:
    TLocalSnapshotReader(
        TDistributedHydraManagerConfigPtr config,
        TCellManagerPtr cellManager,
        IFileSnapshotStorePtr fileStore,
        int snapshotId)
        : Config_(config)
        , CellManager_(cellManager)
        , FileStore_(fileStore)
        , SnapshotId_(snapshotId)
    { }

    TFuture<void> Open() override
    {
        return BIND(&TLocalSnapshotReader::DoOpen, MakeStrong(this))
            .AsyncVia(GetHydraIOInvoker())
            .Run();
    }

    TFuture<TSharedRef> Read() override
    {
        return UnderlyingReader_->Read();
    }

    TSnapshotParams GetParams() const override
    {
        return UnderlyingReader_->GetParams();
    }

private:
    const TDistributedHydraManagerConfigPtr Config_;
    const TCellManagerPtr CellManager_;
    const IFileSnapshotStorePtr FileStore_;
    const int SnapshotId_;

    ISnapshotReaderPtr UnderlyingReader_;


    void DoOpen()
    {
        if (!FileStore_->CheckSnapshotExists(SnapshotId_)) {
            auto asyncResult = DownloadSnapshot(
                Config_,
                CellManager_,
                FileStore_,
                SnapshotId_);
            WaitFor(asyncResult)
                .ThrowOnError();
        }

        UnderlyingReader_ = FileStore_->CreateReader(SnapshotId_);

        WaitFor(UnderlyingReader_->Open())
            .ThrowOnError();
    }

};

////////////////////////////////////////////////////////////////////////////////

class TLocalSnapshotStore
    : public ISnapshotStore
{
public:
    TLocalSnapshotStore(
        TDistributedHydraManagerConfigPtr config,
        TCellManagerPtr cellManager,
        IFileSnapshotStorePtr fileStore)
        : Config_(config)
        , CellManager_(cellManager)
        , FileStore_(fileStore)
    { }

    ISnapshotReaderPtr CreateReader(int snapshotId) override
    {
        return New<TLocalSnapshotReader>(
            Config_,
            CellManager_,
            FileStore_,
            snapshotId);
    }

    ISnapshotWriterPtr CreateWriter(int snapshotId, const TSnapshotMeta& meta) override
    {
        return FileStore_->CreateWriter(snapshotId, meta);
    }

    TFuture<int> GetLatestSnapshotId(int maxSnapshotId) override
    {
        return BIND(&TLocalSnapshotStore::DoGetLatestSnapshotId, MakeStrong(this))
            .AsyncVia(GetHydraIOInvoker())
            .Run(maxSnapshotId);
    }

private:
    const TDistributedHydraManagerConfigPtr Config_;
    const TCellManagerPtr CellManager_;
    const IFileSnapshotStorePtr FileStore_;


    int DoGetLatestSnapshotId(int maxSnapshotId)
    {
        auto asyncParams = DiscoverLatestSnapshot(
            Config_,
            CellManager_,
            maxSnapshotId);
        auto params = WaitFor(asyncParams)
            .ValueOrThrow();
        int localSnapshotId = FileStore_->GetLatestSnapshotId(maxSnapshotId);
        return std::max(localSnapshotId, params.SnapshotId);
    }

};

ISnapshotStorePtr CreateLocalSnapshotStore(
    TDistributedHydraManagerConfigPtr config,
    TCellManagerPtr cellManager,
    IFileSnapshotStorePtr fileStore)
{
    return New<TLocalSnapshotStore>(
        config,
        cellManager,
        fileStore);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHydra
