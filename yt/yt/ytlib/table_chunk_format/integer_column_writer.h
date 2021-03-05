#pragma once

#include "public.h"

#include "column_writer.h"
#include "data_block_writer.h"

#include <yt/yt/client/table_client/versioned_row.h>

namespace NYT::NTableChunkFormat {

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<IValueColumnWriter> CreateVersionedInt64ColumnWriter(
    int columnId,
    bool aggregate,
    TDataBlockWriter* dataBlockWriter);

std::unique_ptr<IValueColumnWriter> CreateVersionedUint64ColumnWriter(
    int columnId,
    bool aggregate,
    TDataBlockWriter* dataBlockWriter);

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<IValueColumnWriter> CreateUnversionedInt64ColumnWriter(
    int columnIndex,
    TDataBlockWriter* dataBlockWriter);

std::unique_ptr<IValueColumnWriter> CreateUnversionedUint64ColumnWriter(
    int columnIndex,
    TDataBlockWriter* dataBlockWriter);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableChunkFormat
