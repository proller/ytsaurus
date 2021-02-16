#pragma once

#include "public.h"
#include "protocol_version.h"

#include <yt/client/api/rpc_proxy/proto/api_service.pb.h>

#include <yt/core/rpc/client.h>

namespace NYT::NApi::NRpcProxy {

////////////////////////////////////////////////////////////////////////////////

class TApiServiceProxy
    : public NRpc::TProxyBase
{
public:
    DEFINE_RPC_PROXY(TApiServiceProxy, ApiService,
        .SetProtocolVersion({
            YTRpcProxyProtocolVersionMajor,
            YTRpcProxyClientProtocolVersionMinor
        }));

    // Transaction server
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GenerateTimestamps);

    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, StartTransaction);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, PingTransaction);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, CommitTransaction);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, FlushTransaction);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, AbortTransaction);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, AttachTransaction);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, DetachTransaction);

    // Cypress server
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ExistsNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, SetNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, MultisetAttributesNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, RemoveNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ListNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, CreateNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, LockNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, UnlockNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, CopyNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, MoveNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, LinkNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ConcatenateNodes);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ExternalizeNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, InternalizeNode);

    // Tablet server
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, MountTable);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, UnmountTable);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, RemountTable);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, FreezeTable);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, UnfreezeTable);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ReshardTable);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ReshardTableAutomatic);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, TrimTable);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, AlterTable);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, AlterTableReplica);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetTablePivotKeys);

    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, LookupRows);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, VersionedLookupRows);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, MultiLookup);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, SelectRows);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ExplainQuery);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetInSyncReplicas);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetTabletInfos);

    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, BalanceTabletCells);

    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ModifyRows);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, BatchModifyRows);

    // Operations
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, StartOperation);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, AbortOperation);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, SuspendOperation);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ResumeOperation);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, CompleteOperation);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, UpdateOperationParameters);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetOperation);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ListOperations);

    // Jobs
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ListJobs);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetJob);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, DumpJobContext);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetJobInput,
        .SetStreamingEnabled(true));
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetJobInputPaths);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetJobSpec);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetJobStderr);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetJobFailContext);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, AbandonJob);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, PollJobShell);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, AbortJob);

    // Files
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ReadFile,
        .SetStreamingEnabled(true));
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, WriteFile,
        .SetStreamingEnabled(true));

    // Journals
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ReadJournal,
        .SetStreamingEnabled(true));
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, WriteJournal,
        .SetStreamingEnabled(true));
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, TruncateJournal);

    // Tables
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, ReadTable,
        .SetStreamingEnabled(true));
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, WriteTable,
        .SetStreamingEnabled(true));

    // File caching
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetFileFromCache);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, PutFileToCache);

    // Object server
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, CreateObject);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetTableMountInfo);

    // Administration
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, BuildSnapshot);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GCCollect);

    // Security
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, AddMember);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, RemoveMember);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, CheckPermission);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, CheckPermissionByAcl);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, TransferAccountResources);

    // Metadata
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetColumnarStatistics);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, CheckClusterLiveness);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NRpcProxy
