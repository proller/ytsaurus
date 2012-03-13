﻿#include "stdafx.h"
#include "sorted_validating_writer.h"

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

TSortedValidatingWriter::TSortedValidatingWriter(
    const TSchema& schema,
    std::vector<TColumn>&& keyColumns,
    IAsyncWriter* writer)
    : TValidatingWriter(schema, MoveRV(keyColumns), writer)
{
    PreviousKey.assign(keyColumns.size(), Stroka());
    Attributes.set_is_sorted(true);
}

TAsyncError::TPtr TSortedValidatingWriter::AsyncEndRow()
{
    if (PreviousKey > CurrentKey) {
        ythrow yexception() << Sprintf(
            "Invalid sorting. Current key %s is greater than previous %s.",
            ~ToString(CurrentKey),
            ~ToString(PreviousKey));
    }
    PreviousKey = CurrentKey;

    return TValidatingWriter::AsyncEndRow();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
