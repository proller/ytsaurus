#pragma once

#include "public.h"
#include "unversioned_row.h"

#include <yt/ytlib/chunk_client/chunk_writer_base.h>

#include <yt/core/actions/future.h>

#include <yt/core/misc/range.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

//! Writes a schemaless unversioned rowset.
/*!
 *  Writes unversioned rowset with schema and variable columns.
 *  Useful for: mapreduce jobs, write command.
 */
struct ISchemalessWriter
    : public virtual NChunkClient::IWriterBase
{
    virtual bool Write(const TRange<TUnversionedRow>& rows) = 0;

    virtual const TNameTablePtr& GetNameTable() const = 0;

    virtual const TTableSchema& GetSchema() const = 0;
};

DEFINE_REFCOUNTED_TYPE(ISchemalessWriter)

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
