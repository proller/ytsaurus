#pragma once

#include "public.h"
#include "row.h"
#include "schema.h"

#include <ytlib/new_table_client/chunk_meta.pb.h>

#include <core/misc/error.h>

namespace NYT {
namespace NVersionedTableClient {

////////////////////////////////////////////////////////////////////////////////

struct IReader
    : public virtual TRefCounted
{
    virtual TAsyncError Open(
        TNameTablePtr nameTable, 
        const NProto::TTableSchemaExt& schema,
        bool includeAllColumns = false,
        ERowsetType type = ERowsetType::Simple) = 0;

    // Returns true while reading is in progress, false when reading is complete.
    // If rows->size() < rows->capacity(), wait for ready event before next call to #Read.
    // Can throw, e.g. if some values in chunk are incompatible with schema.
    // rows must be empty
    virtual bool Read(std::vector<TRow>* rows) = 0;

    virtual TAsyncError GetReadyEvent() = 0;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NVersionedTableClient
} // namespace NYT
