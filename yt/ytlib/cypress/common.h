#pragma once

#include "../misc/common.h"
#include "../misc/enum.h"
#include "../misc/guid.h"
#include "../logging/log.h"

namespace NYT {
namespace NCypress {

////////////////////////////////////////////////////////////////////////////////

extern NLog::TLogger CypressLogger;

////////////////////////////////////////////////////////////////////////////////

DECLARE_ENUM(ERuntimeNodeType,
    // Static types
    ((String)(0))
    ((Int64)(1))
    ((Double)(2))
    ((Map)(3))
    ((List)(4))
    
    // Dynamic types
    ((File)(10))
    ((Table)(11))

    // Virtual types
    ((ChunkMap)(20))
    ((ChunkListMap)(21))
    ((Monitoring)(22))
    ((Orchid)(23))

    // Denotes some uninitialized value.
    ((Invalid)(-1))
    // An upper bound will do.
    ((Last)(30))
);

////////////////////////////////////////////////////////////////////////////////

//! Identifies a node.
typedef TGuid TNodeId;

//! Identifies a lock.
typedef TGuid TLockId;

DECLARE_ENUM(ELockMode,
    (SharedRead)
    (SharedWrite)
    (ExclusiveWrite)
);

extern TNodeId NullNodeId;
extern TNodeId RootNodeId;

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT

