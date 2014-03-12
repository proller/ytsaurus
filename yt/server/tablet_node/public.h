#pragma once

#include <core/misc/common.h>
#include <core/misc/enum.h>

#include <ytlib/election/public.h>

#include <ytlib/tablet_client/public.h>

#include <ytlib/new_table_client/public.h>

#include <ytlib/transaction_client/public.h>

#include <ytlib/chunk_client/public.h>

#include <server/hydra/public.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

using NElection::TCellGuid;
using NElection::NullCellGuid;

using NTabletClient::TTabletCellId;
using NTabletClient::NullTabletCellId;
using NTabletClient::TTabletId;
using NTabletClient::NullTabletId;
using NTabletClient::TStoreId;
using NTabletClient::NullStoreId;

using NTransactionClient::TTransactionId;
using NTransactionClient::NullTransactionId;

using NTransactionClient::TTimestamp;
using NTransactionClient::NullTimestamp;
using NTransactionClient::LastCommittedTimestamp;
using NTransactionClient::AllCommittedTimestamp;

////////////////////////////////////////////////////////////////////////////////
    
DECLARE_ENUM(EPartitionState,
    (None)               // nothing special is happening
    (Splitting)          // split mutation is submitted
    (Merging)            // merge mutation is submitted
    (Compacting)         // compaction (or partitioning) is in progress 
);

DECLARE_ENUM(ETabletState,
    // The only good state admitting read and write requests.
    (Mounted)

    // NB: All states below are for unmounting workflow only!
    (Unmounting)       // transient, requested by master, immediately becomes WaitingForLock
    (WaitingForLocks)
    (RotatingStore)    // transient, immediately becomes FlushingStores
    (FlushingStores)
    (Unmounted)
);

DECLARE_ENUM(EStoreState,
    (ActiveDynamic)         // dynamic, can receive updates
    (PassiveDynamic)        // dynamic, rotated and cannot receive more updates

    (Persistent)            // stored in a chunk

    (Flushing)              // transient, flush is in progress
    (FlushFailed)           // transient, waiting for back off to complete

    (Compacting)            // transient, compaction is in progress
    (CompactionFailed)      // transient, waiting for back off to complete

    (RemoveCommitting)      // UpdateTabletStores request sent
    (RemoveFailed)          // transient, waiting for back off to complete
);

DECLARE_ENUM(EAutomatonThreadQueue,
    (Read)
    (Write)
);

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TTransactionManagerConfig)
DECLARE_REFCOUNTED_CLASS(TTabletManagerConfig)
DECLARE_REFCOUNTED_CLASS(TStoreFlusherConfig)
DECLARE_REFCOUNTED_CLASS(TStoreCompactorConfig)
DECLARE_REFCOUNTED_CLASS(TPartitionBalancerConfig)
DECLARE_REFCOUNTED_CLASS(TTabletNodeConfig)

DECLARE_REFCOUNTED_CLASS(TTabletCellController)
DECLARE_REFCOUNTED_CLASS(TTabletSlot)
DECLARE_REFCOUNTED_CLASS(TTabletAutomaton)

class TSaveContext;
class TLoadContext;

DECLARE_REFCOUNTED_CLASS(TTabletManager)
DECLARE_REFCOUNTED_CLASS(TTransactionManager)

class TPartition;
class TTablet;
class TTransaction;

DECLARE_REFCOUNTED_STRUCT(IStore)

DECLARE_REFCOUNTED_CLASS(TDynamicMemoryStore)
DECLARE_REFCOUNTED_CLASS(TChunkStore)
DECLARE_REFCOUNTED_CLASS(TStoreManager)

struct TDynamicRowHeader;
class TDynamicRow;
struct TDynamicRowRef;

struct TEditListHeader;
template <class T>
class TEditList;
typedef TEditList<NVersionedTableClient::TVersionedValue> TValueList;
typedef TEditList<NVersionedTableClient::TTimestamp> TTimestampList;

class TUnversionedRowMerger;
class TVersionedRowMerger;

typedef NChunkClient::TMultiChunkWriterOptions TTabletWriterOptions;
typedef NChunkClient::TMultiChunkWriterOptionsPtr TTabletWriterOptionsPtr;

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
