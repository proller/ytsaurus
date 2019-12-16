#pragma once

///
/// @file mapreduce/yt/interface/client.h
///
/// Main header of the C++ YT Wrapper.

///
/// @mainpage C++ library for working with YT
///
/// This library provides possibilities to work with YT as a [MapReduce](https://en.wikipedia.org/wiki/MapReduce) sytem. It allows:
///   - to read/write tables and files
///   - to run operations
///   - to work with transactions.
///
/// This library provides only basic functions for working with dynamic tables.
/// To access full powers of YT dynamic tables one should use
/// [yt/client](https://a.yandex-team.ru/arc/trunk/arcadia/yt/19_4/yt/client) library.
///
/// Entry points to this library:
///   - @ref NYT::Initialize() initialization function for this library;
///   - @ref NYT::IClient main interface to work with YT cluster;
///   - @ref NYT::CreateClient() function that creates client for particular cluster;
///   - @ref NYT::IOperationClient ancestor of IClient containing the set of methods to run operations.
///
/// Tutorial on using this library can be found [here](https://wiki.yandex-team.ru/yt/userdoc/cppapi/tutorial/).

#include "fwd.h"

#include "client_method_options.h"
#include "constants.h"
#include "batch_request.h"
#include "cypress.h"
#include "init.h"
#include "io.h"
#include "node.h"
#include "operation.h"

#include <library/threading/future/future.h>

#include <util/datetime/base.h>
#include <util/generic/maybe.h>
#include <util/system/compiler.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

struct TAuthorizationInfo
{
    TString Login;
    TString Realm;
};

////////////////////////////////////////////////////////////////////////////////

struct TCheckPermissionResult
{
    ESecurityAction Action;

    // In case when 'Action == ESecurtiyAction::Deny' because of a 'deny' rule,
    // the "denying" object name and id and "denied" subject name an id may be returned.
    TMaybe<TGUID> ObjectId;
    TMaybe<TString> ObjectName;
    TMaybe<TGUID> SubjectId;
    TMaybe<TString> SubjectName;
};

// The base part of the response corresponds to the check result for the node itself.
// |Columns| contains check results for the columns (in the same order as in the request).
struct TCheckPermissionResponse
    : public TCheckPermissionResult
{
    TVector<TCheckPermissionResult> Columns;
};

////////////////////////////////////////////////////////////////////////////////

class ILock
    : public TThrRefBase
{
public:
    virtual ~ILock() = default;

    /// Get cypress node id of lock itself.
    virtual const TLockId& GetId() const = 0;

    /// Get cypress node id of locked object.
    virtual TNodeId GetLockedNodeId() const = 0;

    ///
    /// @brief Get future that will be set once lock is in "acquired" state.
    ///
    /// Note that future might contain exception if some error occurred
    /// e.g. lock transaction was aborted.
    virtual const NThreading::TFuture<void>& GetAcquiredFuture() const = 0;

    ///
    /// @brief Wait until lock is in "acquired" state.
    ///
    /// Throws exception if timeout exceeded or some error occurred
    /// e.g. lock transaction was aborted.
    void Wait(TDuration timeout = TDuration::Max());
};

////////////////////////////////////////////////////////////////////////////////

/// Base class for @ref NYT::IClient and @ref NYT::ITransaction
class IClientBase
    : public TThrRefBase
    , public ICypressClient
    , public IIOClient
    , public IOperationClient
{
public:
    [[nodiscard]] virtual ITransactionPtr StartTransaction(
        const TStartTransactionOptions& options = TStartTransactionOptions()) = 0;

    ///
    /// @brief Change properties of table.
    ///
    /// Allows to:
    ///   - switch table between dynamic/static mode
    ///   - or change table schema
    virtual void AlterTable(
        const TYPath& path,
        const TAlterTableOptions& options = TAlterTableOptions()) = 0;

    ///
    /// @brief Create batch request object that allows to execute several light requests in parallel.
    ///
    /// @see [YT doc](https://yt.yandex-team.ru/docs//api/commands.html#execute-batch)
    virtual TBatchRequestPtr CreateBatchRequest() = 0;

    /// @brief Get root client outside of all transactions.
    virtual IClientPtr GetParentClient() = 0;
};

////////////////////////////////////////////////////////////////////////////////

class ITransaction
    : virtual public IClientBase
{
public:
    virtual const TTransactionId& GetId() const = 0;

    ///
    /// \brief Try to lock given path.
    ///
    /// Lock will be held until transaction is commited/aborted or `Unlock` method is called.
    /// Lock modes:
    ///  LM_EXCLUSIVE: if exclusive lock is taken no other transaction can take exclusive or shared lock.
    ///  LM_SHARED: if shared lock is taken other transactions can take shared lock but not exclusive.
    ///
    ///  LM_SNAPSHOT: snapshot lock always succeeds, when snapshot lock is taken current transaction snapshots object.
    ///  It will not see changes that occured to it in other transactions.
    ///
    /// Exclusive/shared lock can be waitable or not.
    /// If nonwaitable lock cannot be taken exception is thrown.
    /// If waitable lock cannot be taken it is created in pending state and client can wait until it actually taken.
    /// Check TLockOptions::Waitable and ILock::GetAcquiredFuture for more details.
    virtual ILockPtr Lock(
        const TYPath& path,
        ELockMode mode,
        const TLockOptions& options = TLockOptions()) = 0;

    ///
    /// \brief Remove all the locks (including pending ones) for this transaction from a Cypress node at `path`.
    ///
    /// If the locked version of the node differs from the original one,
    /// an error will be thrown.
    ///
    /// Command is successful even if the node has no locks.
    /// Only explicit (created by `Lock` method) locks are removed.
    virtual void Unlock(
        const TYPath& path,
        const TUnlockOptions& options = TUnlockOptions()) = 0;

    ///
    /// \brief Commit transaction.
    ///
    /// All changes that are made by transactions become visible globaly or to parent transaction.
    virtual void Commit() = 0;

    ///
    /// \brief Abort transaction.
    ///
    /// All changes that are made by current transaction are lost.
    virtual void Abort() = 0;

    /// Ping transaction.
    virtual void Ping() = 0;

    //
    // Detach transaction.
    // Stop any activities connected with it: pinging, aborting on crashed etc.
    // Forget about the transaction totally.
    virtual void Detach();
};

////////////////////////////////////////////////////////////////////////////////

class IClient
    : virtual public IClientBase
{
public:
    ///
    /// @brief Attach to existing transaction.
    ///
    /// Returned object WILL NOT:
    ///  - ping transaction automatically (unless @ref TAttachTransactionOptions::AutoPing is set)
    ///  - abort it on program termination (unless @ref TAttachTransactionOptions::AbortOnTermination is set).
    /// Otherwise returned object is similar to the object returned by @ref IClientBase::StartTransaction.
    /// and it can see all the changes made inside the transaction.
    [[nodiscard]] virtual ITransactionPtr AttachTransaction(
        const TTransactionId& transactionId,
        const TAttachTransactionOptions& options = TAttachTransactionOptions()) = 0;

    virtual void MountTable(
        const TYPath& path,
        const TMountTableOptions& options = TMountTableOptions()) = 0;

    virtual void UnmountTable(
        const TYPath& path,
        const TUnmountTableOptions& options = TUnmountTableOptions()) = 0;

    virtual void RemountTable(
        const TYPath& path,
        const TRemountTableOptions& options = TRemountTableOptions()) = 0;

    ///
    /// @brief Switch dynamic table from `mounted' into `frozen' state.
    ///
    /// When table is in frozen state all its data is flushed to disk and writes are disabled.
    ///
    /// NOTE: this function launches the process of switching, but doesn't wait until switching is accomplished.
    /// Waiting has to be performed by user.
    virtual void FreezeTable(
        const TYPath& path,
        const TFreezeTableOptions& options = TFreezeTableOptions()) = 0;

    ///
    /// @brief Switch dynamic table from `frozen' into `mounted' state.
    ///
    /// NOTE: this function launches the process of switching, but doesn't wait until switching is accomplished.
    /// Waiting has to be performed by user.
    virtual void UnfreezeTable(
        const TYPath& path,
        const TUnfreezeTableOptions& options = TUnfreezeTableOptions()) = 0;

    virtual void ReshardTable(
        const TYPath& path,
        const TVector<TKey>& pivotKeys,
        const TReshardTableOptions& options = TReshardTableOptions()) = 0;

    virtual void ReshardTable(
        const TYPath& path,
        i64 tabletCount,
        const TReshardTableOptions& options = TReshardTableOptions()) = 0;

    virtual void InsertRows(
        const TYPath& path,
        const TNode::TListType& rows,
        const TInsertRowsOptions& options = TInsertRowsOptions()) = 0;

    virtual void DeleteRows(
        const TYPath& path,
        const TNode::TListType& keys,
        const TDeleteRowsOptions& options = TDeleteRowsOptions()) = 0;

    virtual void TrimRows(
        const TYPath& path,
        i64 tabletIndex,
        i64 rowCount,
        const TTrimRowsOptions& options = TTrimRowsOptions()) = 0;

    virtual TNode::TListType LookupRows(
        const TYPath& path,
        const TNode::TListType& keys,
        const TLookupRowsOptions& options = TLookupRowsOptions()) = 0;

    virtual TNode::TListType SelectRows(
        const TString& query,
        const TSelectRowsOptions& options = TSelectRowsOptions()) = 0;

    ///
    /// @brief Change properties of table replica.
    ///
    /// Allows to enable/disable replica and/or change its mode.
    virtual void AlterTableReplica(
        const TReplicaId& replicaId,
        const TAlterTableReplicaOptions& alterTableReplicaOptions) = 0;

    virtual ui64 GenerateTimestamp() = 0;

    /// Return YT username of current client.
    virtual TAuthorizationInfo WhoAmI() = 0;

    /// Get operation attributes.
    virtual TOperationAttributes GetOperation(
        const TOperationId& operationId,
        const TGetOperationOptions& options = TGetOperationOptions()) = 0;

    /// List operations satisfying given filters.
    virtual TListOperationsResult ListOperations(
        const TListOperationsOptions& options = TListOperationsOptions()) = 0;

    /// Update operation runtime parameters.
    virtual void UpdateOperationParameters(
        const TOperationId& operationId,
        const TUpdateOperationParametersOptions& options) = 0;

    /// Get job attributes.
    virtual TJobAttributes GetJob(
        const TOperationId& operationId,
        const TJobId& jobId,
        const TGetJobOptions& options = TGetJobOptions()) = 0;

    /// List jobs satisfying given filters.
    virtual TListJobsResult ListJobs(
        const TOperationId& operationId,
        const TListJobsOptions& options = TListJobsOptions()) = 0;

    ///
    /// @brief Get the input of a running or failed job.
    ///
    /// TErrorResponse exception is thrown if job is missing.
    virtual IFileReaderPtr GetJobInput(
        const TJobId& jobId,
        const TGetJobInputOptions& options = TGetJobInputOptions()) = 0;

    ///
    /// @brief Get fail context of a failed job.
    ///
    /// TErrorResponse exception is thrown if it is missing.
    virtual IFileReaderPtr GetJobFailContext(
        const TOperationId& operationId,
        const TJobId& jobId,
        const TGetJobFailContextOptions& options = TGetJobFailContextOptions()) = 0;

    ///
    /// @brief Get stderr of a running or failed job
    ///
    /// TErrorResponse exception is thrown if it is missing.
    virtual IFileReaderPtr GetJobStderr(
        const TOperationId& operationId,
        const TJobId& jobId,
        const TGetJobStderrOptions& options = TGetJobStderrOptions()) = 0;

    ///
    /// @brief Create rbtorrent for given table written in special format
    ///
    /// [More info.](https://wiki.yandex-team.ru/yt/userdoc/blob_tables/#shag3.sozdajomrazdachu)
    virtual TString SkyShareTable(const TYPath& tablePath) = 0;

    ///
    /// @brief Create a set of rbtorrents, one torrent for each value of keyColumns columns
    ///
    /// @return list of nodes, each node has two fields
    /// * key: list of key columns values
    /// * rbtorrent: rbtorrent string
    virtual TNode::TListType SkyShareTableByKey(
        const TYPath& tablePath,
        const TKeyColumns& keyColumns) = 0;

    ///
    /// @brief Check if 'user' has 'permission' to access a Cypress node at 'path'.
    ///
    /// For tables access to columns specified in 'options.Columns_' can be checked
    /// (see https://wiki.yandex-team.ru/yt/userdoc/columnaracl).
    ///
    /// If access is denied (the returned result has '.Action == ESecurityAction::Deny')
    /// because of a 'deny' rule, the "denying" object name and id
    /// and "denied" subject name an id may be returned.
    virtual TCheckPermissionResponse CheckPermission(
        const TString& user,
        EPermission permission,
        const TYPath& path,
        const TCheckPermissionOptions& options = TCheckPermissionOptions()) = 0;

    ///
    /// @brief Suspend operation.
    ///
    /// Jobs will be aborted.
    virtual void SuspendOperation(
        const TOperationId& operationId,
        const TSuspendOperationOptions& options = TSuspendOperationOptions()) = 0;

    /// Resume previously suspended operation.
    virtual void ResumeOperation(
        const TOperationId& operationId,
        const TResumeOperationOptions& options = TResumeOperationOptions()) = 0;
};


/// Create a client for particular mapreduce cluster.
IClientPtr CreateClient(
    const TString& serverName,
    const TCreateClientOptions& options = TCreateClientOptions());

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
