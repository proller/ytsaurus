#include <yt/core/test_framework/framework.h>

#include "column_format_ut.h"

#include <yt/ytlib/table_chunk_format/double_column_writer.h>
#include <yt/ytlib/table_chunk_format/double_column_reader.h>

#include <yt/ytlib/table_chunk_format/helpers.h>

namespace NYT::NTableClient {
namespace {

using namespace NTableChunkFormat;

////////////////////////////////////////////////////////////////////////////////

class TVersionedDoubleColumnTest
    : public TVersionedColumnTestBase
{
protected:
    const TTimestamp TimestampBase = 1000000;

    struct TValue
    {
        std::optional<double> Data;
        TTimestamp Timestamp;
    };

    TVersionedDoubleColumnTest()
        : TVersionedColumnTestBase(false)
    { }

    TVersionedValue MakeValue(const TValue& value) const
    {
        if (value.Data) {
            return MakeVersionedDoubleValue(
                *value.Data,
                value.Timestamp,
                ColumnId,
                false);
        } else {
            return MakeVersionedSentinelValue(
                EValueType::Null,
                value.Timestamp,
                ColumnId,
                false);
        }
    }

    TVersionedRow CreateRow(const std::vector<TValue>& values) const
    {
        std::vector<TVersionedValue> versionedValues;
        for (const auto& value : values) {
            versionedValues.push_back(MakeValue(value));
        }
        return CreateRowWithValues(versionedValues);
    }

    void AppendExtremeRow(std::vector<TVersionedRow>* rows)
    {
        rows->push_back(CreateRow({
            {std::numeric_limits<double>::max(), TimestampBase},
            {std::numeric_limits<double>::min(), TimestampBase + 1},
            {std::nullopt, TimestampBase + 2}}));
    }

    std::vector<TVersionedRow> CreateDirectDense()
    {
        std::vector<TVersionedRow> rows;

        for (int i = 0; i < 100 * 100; ++i) {
            rows.push_back(CreateRow({
                {i * 3.14 , TimestampBase + i * 10},
                {i * 10 * 3.14, TimestampBase + (i + 2) * 10}}));
        }
        rows.push_back(CreateRowWithValues({}));
        AppendExtremeRow(&rows);
        return rows;
    }

    std::vector<TVersionedRow> CreateDirectSparse()
    {
        std::vector<TVersionedRow> rows;
        for (int i = 0; i < 1000; ++i) {
            rows.push_back(CreateRow({
                {i * 3.14, TimestampBase + i * 10},
                {i * 10 * 3.14, TimestampBase + (i + 2) * 10}}));

            for (int j = 0; j < 10; ++j) {
                rows.push_back(CreateRowWithValues({}));
            }
        }
        AppendExtremeRow(&rows);
        return rows;
    }

    std::vector<TVersionedRow> GetOriginalRows()
    {
        std::vector<TVersionedRow> expected;
        AppendVector(&expected, CreateDirectDense());
        AppendVector(&expected, CreateDirectSparse());
        return expected;
    }

    virtual void Write(IValueColumnWriter* columnWriter) override
    {
        WriteSegment(columnWriter, CreateDirectDense());
        WriteSegment(columnWriter, CreateDirectSparse());
    }

    void DoReadValues(TTimestamp timestamp, int padding = 500)
    {
        auto originalRows = GetOriginalRows();
        Validate(originalRows, padding, originalRows.size() - padding, timestamp);
    }

    virtual std::unique_ptr<IVersionedColumnReader> DoCreateColumnReader() override
    {
        return CreateVersionedDoubleColumnReader(ColumnMeta_, ColumnId, Aggregate_);
    }

    virtual std::unique_ptr<IValueColumnWriter> CreateColumnWriter(TDataBlockWriter* blockWriter) override
    {
        return CreateVersionedDoubleColumnWriter(ColumnId, Aggregate_, blockWriter);
    }
};

TEST_F(TVersionedDoubleColumnTest, ReadValues)
{
    DoReadValues(TimestampBase + 80000);
}

TEST_F(TVersionedDoubleColumnTest, ReadValuesMinTimestamp)
{
    DoReadValues(MinTimestamp);
}

TEST_F(TVersionedDoubleColumnTest, ReadValuesMaxTimestamp)
{
    DoReadValues(MaxTimestamp);
}

////////////////////////////////////////////////////////////////////////////////

class TUnversionedDoubleColumnTest
    : public TUnversionedColumnTestBase<double>
{
protected:
    using TUnversionedColumnTestBase<double>::CreateColumnReader;

    virtual std::optional<double> DecodeValueFromColumn(
        const IUnversionedRowBatch::TColumn* column,
        i64 index) override
    {
        YT_VERIFY(column->StartIndex >= 0);
        index += column->StartIndex;
        
        ResolveRleEncoding(column, index);
        
        if (IsColumnValueNull(column, index)) {
            return std::nullopt;
        }
        
        return DecodeDoubleFromColumn(*column, index);
    }

    virtual void Write(IValueColumnWriter* columnWriter) override
    {
        // Segment 1 - 5 values.
        WriteSegment(columnWriter, {std::nullopt, 1.0, 2.0, 3.0, 4.0});
        // Segment 2 - 1 value.
        WriteSegment(columnWriter, {5.0});
        // Segment 3 - 4 values.
        WriteSegment(columnWriter, {6.0, 7.0, 8.0, 9.0});
    }

    virtual std::unique_ptr<IUnversionedColumnReader> DoCreateColumnReader() override
    {
        return CreateUnversionedDoubleColumnReader(ColumnMeta_, ColumnIndex, ColumnId);
    }

    virtual std::unique_ptr<IValueColumnWriter> CreateColumnWriter(TDataBlockWriter* blockWriter) override
    {
        return CreateUnversionedDoubleColumnWriter(
            ColumnIndex,
            blockWriter);
    }
};

////////////////////////////////////////////////////////////////////////////////

TEST_F(TUnversionedDoubleColumnTest, GetEqualRange)
{
    auto reader = CreateColumnReader();
    EXPECT_EQ(std::make_pair(8L, 8L), reader->GetEqualRange(MakeValue(7.5), 7, 8));
    EXPECT_EQ(std::make_pair(0L, 0L), reader->GetEqualRange(MakeValue(std::nullopt), 0, 0));
    EXPECT_EQ(std::make_pair(8L, 8L), reader->GetEqualRange(MakeValue(7.5), 2, 9));
}

TEST_F(TUnversionedDoubleColumnTest, ReadValues)
{
    auto actual = AllocateRows(3);

    // Test null rows.
    actual.push_back(TMutableVersionedRow());

    auto reader = CreateColumnReader();
    reader->SkipToRowIndex(3);
    reader->ReadValues(TMutableRange<TMutableVersionedRow>(actual.data(), actual.size()));

    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(0, CompareRowValues(MakeValue(3.0 + i), *actual[i].BeginKeys()));
    }
}

TEST_F(TUnversionedDoubleColumnTest, ReadNull)
{
    auto reader = CreateColumnReader();
    auto rows = AllocateRows(3);
    reader->ReadValues(TMutableRange<TMutableVersionedRow>(rows.data(), rows.size()));
    EXPECT_EQ(MakeValue(std::nullopt), *rows.front().BeginKeys());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NTableClient
