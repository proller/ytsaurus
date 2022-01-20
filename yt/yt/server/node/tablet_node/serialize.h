#pragma once

#include "public.h"

#include <yt/yt/server/lib/hydra_common/composite_automaton.h>

namespace NYT::NTabletNode {

////////////////////////////////////////////////////////////////////////////////

// If reign change is disallowed, tablet node will crash if cell snapshot reign
// differs from node reign. This is useful for local mode where occasional cell
// state migration may end up with a disaster.
void SetReignChangeAllowed(bool allowed);
bool IsReignChangeAllowed();

////////////////////////////////////////////////////////////////////////////////

NHydra::TReign GetCurrentReign();
bool ValidateSnapshotReign(NHydra::TReign);
NHydra::EFinalRecoveryAction GetActionToRecoverFromReign(NHydra::TReign reign);

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(ETabletReign,
    // 21.2 starts here.
    ((RowBufferEmptyRowDeserialization)   (100200)) // max42
    ((Hunks1)                             (100201)) // babenko
    ((Hunks2)                             (100202)) // babenko
    ((PersistChunkTimestamp)              (100203)) // ifsmirnov
    ((SchemaIdUponMount)                  (100204)) // akozhikhov
    ((VersionedWriteToOrderedTablet)      (100205)) // gritukan
    // 21.3 starts here.
    ((WriteGenerations)                   (100301)) // max42
    // 22.1 starts here.
    ((NewLockMasks)                       (100400)) // gritukan
    ((Chaos)                              (100401)) // savrus
    ((BackupsSorted)                      (100402)) // ifsmirnov
    ((MaxClipTimestamp)                   (100403)) // ifsmirnov
);

////////////////////////////////////////////////////////////////////////////////

class TSaveContext
    : public NHydra::TSaveContext
{
public:
    ETabletReign GetVersion() const;
};

////////////////////////////////////////////////////////////////////////////////

class TLoadContext
    : public NHydra::TLoadContext
{
public:
    ETabletReign GetVersion() const;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
