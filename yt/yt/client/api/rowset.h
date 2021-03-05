#pragma once

#include "public.h"

#include <yt/yt/client/table_client/public.h>

#include <yt/yt/client/tablet_client/public.h>

#include <yt/yt/core/actions/future.h>

#include <yt/yt/core/misc/shared_range.h>

namespace NYT::NApi {

////////////////////////////////////////////////////////////////////////////////

template <class TRow>
struct IRowset
    : public virtual TRefCounted
{
    // TODO(babenko): refcounted schema
    virtual const NTableClient::TTableSchema& GetSchema() const = 0;
    virtual const NTableClient::TNameTablePtr& GetNameTable() const = 0;

    virtual TRange<TRow> GetRows() const = 0;
};

DEFINE_REFCOUNTED_TYPE(IUnversionedRowset)
DEFINE_REFCOUNTED_TYPE(IVersionedRowset)

////////////////////////////////////////////////////////////////////////////////

template <class TRow>
IRowsetPtr<TRow> CreateRowset(
    NTableClient::TTableSchemaPtr schema,
    TSharedRange<TRow> rows);

template <class TRow>
IRowsetPtr<TRow> CreateRowset(
    NTableClient::TNameTablePtr nameTable,
    TSharedRange<TRow> rows);

std::tuple<NTableClient::IUnversionedRowsetWriterPtr, TFuture<IUnversionedRowsetPtr>>
    CreateSchemafulRowsetWriter(NTableClient::TTableSchemaPtr schema);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi

