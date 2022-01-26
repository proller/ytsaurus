#include "snapshot_download.h"
#include "private.h"
#include "snapshot_discovery.h"
#include "file_snapshot_store.h"

#include <yt/yt/server/lib/hydra_common/config.h>
#include <yt/yt/server/lib/hydra_common/snapshot.h>
#include <yt/yt/server/lib/hydra_common/snapshot_service_proxy.h>

#include <yt/yt/ytlib/election/cell_manager.h>

#include <yt/yt/core/concurrency/scheduler.h>

namespace NYT::NHydra {

using namespace NElection;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

namespace {

void DoDownloadSnapshot(
    const TDistributedHydraManagerConfigPtr& config,
    const TCellManagerPtr& cellManager,
    const IFileSnapshotStorePtr& fileStore,
    int snapshotId)
{
    auto Logger = HydraLogger.WithTag("SnapshotId: %v, CellId: %v, SelfPeerId: %v",
        snapshotId,
        cellManager->GetCellId(),
        cellManager->GetSelfPeerId());

    try {
        YT_LOG_INFO("Will download snapshot from peers");

        auto params = WaitFor(DiscoverSnapshot(config, cellManager, snapshotId))
            .ValueOrThrow();

        auto writer = fileStore->CreateRawWriter(snapshotId);
        WaitFor(writer->Open())
            .ThrowOnError();

        YT_LOG_INFO("Downloading snapshot from peer (CompressedLength: %v, PeerId: %v)",
            params.CompressedLength,
            params.PeerId);

        TSnapshotServiceProxy proxy(cellManager->GetPeerChannel(params.PeerId));
        proxy.SetDefaultTimeout(config->SnapshotDownloadRpcTimeout);

        i64 downloadedLength = 0;
        while (downloadedLength < params.CompressedLength) {
            auto req = proxy.ReadSnapshot();
            req->set_snapshot_id(snapshotId);
            req->set_offset(downloadedLength);
            i64 desiredBlockSize = std::min(
                config->SnapshotDownloadBlockSize,
                params.CompressedLength - downloadedLength);
            req->set_length(desiredBlockSize);

            auto rspOrError = WaitFor(req->Invoke());
            THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error downloading snapshot");
            const auto& rsp = rspOrError.Value();

            const auto& attachments = rsp->Attachments();
            YT_VERIFY(attachments.size() == 1);

            const auto& block = attachments[0];
            YT_LOG_DEBUG("Snapshot block received (Offset: %v, Size: %v)",
                downloadedLength,
                block.Size());

            WaitFor(writer->Write(block))
                .ThrowOnError();

            downloadedLength += block.Size();
        }

        WaitFor(writer->Close())
            .ThrowOnError();

        YT_LOG_INFO("Snapshot downloaded successfully");
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION("Error downloading snapshot %v", snapshotId)
           << ex;
    }
}

} // namespace

TFuture<void> DownloadSnapshot(
    TDistributedHydraManagerConfigPtr config,
    TCellManagerPtr cellManager,
    IFileSnapshotStorePtr fileStore,
    int snapshotId)
{
    return BIND(DoDownloadSnapshot)
        .AsyncVia(GetCurrentInvoker())
        .Run(std::move(config), std::move(cellManager), std::move(fileStore), snapshotId);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHydra
