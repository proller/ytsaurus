#pragma once

#include "public.h"

#include "column_reader.h"
#include "bit_packed_unsigned_vector.h"
#include "helpers.h"

#include <yt/client/table_chunk_format/proto/column_meta.pb.h>

#include <yt/client/table_client/versioned_row.h>
#include <yt/client/table_client/logical_type.h>

#include <yt/core/misc/bitmap.h>
#include <yt/core/misc/zigzag.h>
#include <yt/core/misc/algorithm_helpers.h>

namespace NYT::NTableChunkFormat {

////////////////////////////////////////////////////////////////////////////////

struct ISegmentReaderBase
    : public TNonCopyable
{
    virtual ~ISegmentReaderBase() = default;
    
    virtual void SkipToRowIndex(i64 rowIndex) = 0;
};

////////////////////////////////////////////////////////////////////////////////

struct IUnversionedSegmentReader
    : public ISegmentReaderBase
{
    virtual i64 ReadValues(TMutableRange<NTableClient::TMutableVersionedRow> rows) = 0;
    virtual i64 ReadValues(TMutableRange<NTableClient::TMutableUnversionedRow> rows) = 0;

    virtual int GetBatchColumnCount() = 0;
    virtual void ReadColumnarBatch(
        TMutableRange<NTableClient::IUnversionedRowBatch::TColumn> columns,
        i64 rowCount) = 0;

    //! Last value of the segment.
    virtual NTableClient::TUnversionedValue GetLastValue() const = 0;

    virtual i64 GetLowerRowIndex(
        const NTableClient::TUnversionedValue& value,
        i64 rowIndexLimit) const = 0;
    virtual i64 GetUpperRowIndex(
        const NTableClient::TUnversionedValue& value,
        i64 rowIndexLimit) const = 0;

    virtual i64 EstimateDataWeight(
        i64 lowerRowIndex,
        i64 upperRowIndex) = 0;
};

////////////////////////////////////////////////////////////////////////////////

struct IVersionedSegmentReader
    : public ISegmentReaderBase
{
    //! Transactional read.
    virtual i64 ReadValues(
        TMutableRange<NTableClient::TMutableVersionedRow> rows,
        TRange<std::pair<ui32, ui32>> timestampIndexRanges,
        bool produceAllVersions) = 0;

    //! Compaction read.
    virtual i64 ReadAllValues(TMutableRange<NTableClient::TMutableVersionedRow> rows) = 0;

    virtual void ReadValueCounts(TMutableRange<ui32> valueCounts) const = 0;
};

////////////////////////////////////////////////////////////////////////////////

class TUnversionedSegmentReaderBase
    : public IUnversionedSegmentReader
{
public:
    TUnversionedSegmentReaderBase(
        TRef data,
        const NProto::TSegmentMeta& meta,
        int columnIndex,
        int columnId,
        NTableClient::EValueType valueType);

    virtual i64 EstimateDataWeight(i64 lowerRowIndex, i64 upperRowIndex) override;

protected:
    const TRef Data_;
    const NProto::TSegmentMeta& Meta_;
    const int ColumnIndex_;
    const int ColumnId_;
    const NTableClient::EValueType ValueType_;

    const i64 SegmentStartRowIndex_;

    i64 SegmentRowIndex_ = 0;

    
    i64 GetSegmentRowIndex(i64 rowIndex) const;
    
    template <class TValueExtractor>
    void DoReadColumnarBatch(
        TValueExtractor* valueExtractor,
        TMutableRange<NTableClient::IUnversionedRowBatch::TColumn> columns,
        i64 rowCount)
    {
        valueExtractor->ReadColumnarBatch(SegmentRowIndex_, rowCount, columns);
        SegmentRowIndex_ += rowCount;
        YT_VERIFY(SegmentRowIndex_ <= Meta_.row_count());
    }
};

////////////////////////////////////////////////////////////////////////////////

template <NTableClient::EValueType ValueType, class TValueExtractor>
class TDenseUnversionedSegmentReader
    : public TUnversionedSegmentReaderBase
{
public:
    TDenseUnversionedSegmentReader(
        TRef data,
        const NProto::TSegmentMeta& meta,
        int columnIndex,
        int columnId)
        : TUnversionedSegmentReaderBase(data, meta, columnIndex, columnId, ValueType)
        , ValueExtractor_(data, meta)
    { }

    virtual i64 GetLowerRowIndex(const NTableClient::TUnversionedValue& value, i64 upperRowIndex) const override
    {
        i64 index = BinarySearch(
            SegmentRowIndex_,
            std::min(GetSegmentRowIndex(upperRowIndex), Meta_.row_count()),
            [&] (i64 segmentRowIndex) {
                NTableClient::TUnversionedValue currentValue;
                SetValue(&currentValue, segmentRowIndex);
                return CompareValues<ValueType>(currentValue, value) < 0;
            });
        return SegmentStartRowIndex_ + index;
    }

    virtual i64 GetUpperRowIndex(const NTableClient::TUnversionedValue& value, i64 upperRowIndex) const override
    {
        i64 index = BinarySearch(
            SegmentRowIndex_,
            std::min(GetSegmentRowIndex(upperRowIndex), Meta_.row_count()),
            [&] (i64 segmentRowIndex) {
                NTableClient::TUnversionedValue currentValue;
                SetValue(&currentValue, segmentRowIndex);
                return CompareValues<ValueType>(currentValue, value) <= 0;
            });
        return SegmentStartRowIndex_ + index;
    }

    virtual void SkipToRowIndex(i64 rowIndex) override
    {
        YT_VERIFY(GetSegmentRowIndex(rowIndex) >= SegmentRowIndex_);
        SegmentRowIndex_ = GetSegmentRowIndex(rowIndex);
    }

    virtual NTableClient::TUnversionedValue GetLastValue() const override
    {
        NTableClient::TUnversionedValue value;
        SetValue(&value, Meta_.row_count() - 1);
        return value;
    }

    virtual i64 ReadValues(TMutableRange<NTableClient::TMutableVersionedRow> rows) override
    {
        return DoReadValues(rows);
    }

    virtual i64 ReadValues(TMutableRange<NTableClient::TMutableUnversionedRow> rows) override
    {
        return DoReadValues(rows);
    }

    virtual int GetBatchColumnCount() override
    {
        return ValueExtractor_.GetBatchColumnCount();
    }

    virtual void ReadColumnarBatch(
        TMutableRange<NTableClient::IUnversionedRowBatch::TColumn> columns,
        i64 rowCount) override
    {
        TUnversionedSegmentReaderBase::DoReadColumnarBatch(&ValueExtractor_, columns, rowCount);
    }

private:
    TValueExtractor ValueExtractor_;

    
    void SetValue(NTableClient::TUnversionedValue* value, i64 rowIndex) const
    {
        ValueExtractor_.ExtractValue(value, rowIndex, ColumnId_, false);
    }

    template<class TRow>
    i64 DoReadValues(TMutableRange<TRow> rows)
    {
        i64 rangeRowIndex = 0;
        i64 segmentRowIndex = SegmentRowIndex_;

        while (rangeRowIndex < rows.Size() && segmentRowIndex < Meta_.row_count()) {
            auto row = rows[rangeRowIndex];
            if (row) {
                YT_VERIFY(GetUnversionedValueCount(row) > ColumnIndex_);
                SetValue(&GetUnversionedValue(row, ColumnIndex_), segmentRowIndex);
            }

            ++segmentRowIndex;
            ++rangeRowIndex;
        }

        SegmentRowIndex_ = segmentRowIndex;
        return rangeRowIndex;
    }
};

////////////////////////////////////////////////////////////////////////////////

template <bool Scan>
class TRleValueExtractorBase
{
public:
    i64 GetValueCount() const
    {
        return RowIndexReader_.GetSize();
    }

    i64 GetRowIndex(i64 valueIndex) const
    {
        return RowIndexReader_[valueIndex];
    }

protected:
    using TRowIndexReader = TBitPackedUnsignedVectorReader<ui64, Scan>;
    TRowIndexReader RowIndexReader_;
};

////////////////////////////////////////////////////////////////////////////////

template <NTableClient::EValueType ValueType, class TRleValueExtractor>
class TRleUnversionedSegmentReader
    : public TUnversionedSegmentReaderBase
{
public:
    TRleUnversionedSegmentReader(
        TRef data,
        const NProto::TSegmentMeta& meta,
        int columnIndex,
        int columnId)
        : TUnversionedSegmentReaderBase(data, meta, columnIndex, columnId, ValueType)
        , ValueExtractor_(data, meta)
    { }

    virtual void SkipToRowIndex(i64 rowIndex) override
    {
        auto segmentRowIndex = GetSegmentRowIndex(rowIndex);
        YT_VERIFY(segmentRowIndex >= SegmentRowIndex_);
        if (segmentRowIndex > SegmentRowIndex_) {
            SegmentRowIndex_ = segmentRowIndex;

            ValueIndex_ = BinarySearch(
                ValueIndex_,
                ValueExtractor_.GetValueCount(),
                [&] (i64 valueIndex) {
                    return ValueExtractor_.GetRowIndex(valueIndex) <= SegmentRowIndex_;
                }) - 1;
        }
    }

    virtual NTableClient::TUnversionedValue GetLastValue() const override
    {
        NTableClient::TUnversionedValue value;
        SetValue(&value, ValueExtractor_.GetValueCount() - 1);
        return value;
    }

    virtual i64 GetLowerRowIndex(const NTableClient::TUnversionedValue& value, i64 rowIndexLimit) const override
    {
        i64 upperValueIndex = GetUpperValueIndex(rowIndexLimit);
        i64 valueIndex = BinarySearch(
            ValueIndex_,
            upperValueIndex,
            [&] (i64 valueIndex) {
                NTableClient::TUnversionedValue currentValue;
                SetValue(&currentValue, valueIndex);
                return CompareValues<ValueType>(currentValue, value) < 0;
            });

        return std::min(GetValueLowerRowIndex(valueIndex), rowIndexLimit);
    }

    virtual i64 GetUpperRowIndex(const NTableClient::TUnversionedValue& value, i64 rowIndexLimit) const override
    {
        i64 upperValueIndex = GetUpperValueIndex(rowIndexLimit);
        i64 valueIndex = BinarySearch(
            ValueIndex_,
            upperValueIndex,
            [&] (i64 valueIndex) {
                NTableClient::TUnversionedValue currentValue;
                SetValue(&currentValue, valueIndex);
                return CompareValues<ValueType>(currentValue, value) <= 0;
            });

        return std::min(GetValueLowerRowIndex(valueIndex), rowIndexLimit);
    }

    virtual i64 ReadValues(TMutableRange<NTableClient::TMutableVersionedRow> rows) override
    {
        return DoReadValues(rows);
    }

    virtual i64 ReadValues(TMutableRange<NTableClient::TMutableUnversionedRow> rows) override
    {
        return DoReadValues(rows);
    }

    virtual int GetBatchColumnCount() override
    {
        return ValueExtractor_.GetBatchColumnCount();
    }

    virtual void ReadColumnarBatch(
        TMutableRange<NTableClient::IUnversionedRowBatch::TColumn> columns,
        i64 rowCount) override
    {
        TUnversionedSegmentReaderBase::DoReadColumnarBatch(&ValueExtractor_, columns, rowCount);
    }

private:
    TRleValueExtractor ValueExtractor_;
    i64 ValueIndex_ = 0;

    
    i64 GetUpperValueIndex(i64 rowIndex) const
    {
        i64 upperValueIndex;
        if (GetSegmentRowIndex(rowIndex) >= Meta_.row_count()) {
            upperValueIndex = ValueExtractor_.GetValueCount();
        } else {
            upperValueIndex = BinarySearch(
                ValueIndex_,
                ValueExtractor_.GetValueCount(),
                [&] (i64 valueIndex) {
                    return ValueExtractor_.GetRowIndex(valueIndex) < GetSegmentRowIndex(rowIndex);
                });
        }
        return upperValueIndex;
    }

    i64 GetValueLowerRowIndex(i64 valueIndex) const
    {
        return SegmentStartRowIndex_ + std::max(
            SegmentRowIndex_,
            valueIndex < ValueExtractor_.GetValueCount()
                ? ValueExtractor_.GetRowIndex(valueIndex)
                : Meta_.row_count());
    }

    void SetValue(NTableClient::TUnversionedValue* value) const
    {
        SetValue(value, ValueIndex_);
    }

    void SetValue(NTableClient::TUnversionedValue* value, i64 valueIndex) const
    {
        ValueExtractor_.ExtractValue(value, valueIndex, ColumnId_, false);
    }

    template <class TRow>
    i64 DoReadValues(TMutableRange<TRow> rows)
    {
        i64 rangeRowIndex = 0;
        while (rangeRowIndex < rows.Size() && SegmentRowIndex_ < Meta_.row_count()) {
            i64 valueRowCount = ValueIndex_ + 1 == ValueExtractor_.GetValueCount()
                ? Meta_.row_count()
                : ValueExtractor_.GetRowIndex(ValueIndex_ + 1);
            i64 segmentRowIndex = SegmentRowIndex_;

            NTableClient::TUnversionedValue value;
            SetValue(&value);

            while (segmentRowIndex < valueRowCount && rangeRowIndex < rows.Size()) {
                auto row = rows[rangeRowIndex];
                if (row) {
                    GetUnversionedValue(row, ColumnIndex_) = value;
                }
                ++rangeRowIndex;
                ++segmentRowIndex;
            }

            SegmentRowIndex_ = segmentRowIndex;
            if (SegmentRowIndex_ == valueRowCount) {
                ++ValueIndex_;
            }
        }
        return rangeRowIndex;
    }
};

////////////////////////////////////////////////////////////////////////////////

class TColumnReaderBase
    : public virtual IColumnReaderBase
{
public:
    explicit TColumnReaderBase(const NProto::TColumnMeta& columnMeta);

    virtual void Rearm() override;
    virtual void SetCurrentBlock(TSharedRef block, int blockIndex) override;

    virtual void SkipToRowIndex(i64 rowIndex) override;
    virtual i64 GetCurrentRowIndex() const override;
    virtual i64 GetBlockUpperRowIndex() const override;
    virtual i64 GetReadyUpperRowIndex() const override;
    
    virtual int GetCurrentBlockIndex() const override;
    virtual std::optional<int> GetNextBlockIndex() const override;

protected:
    const NProto::TColumnMeta& ColumnMeta_;

    TSharedRef Block_;
    int CurrentBlockIndex_ = -1;
    int CurrentSegmentIndex_ = 0;
    i64 CurrentRowIndex_ = 0;

    //! Index of the last segment in the current block.
    int LastBlockSegmentIndex_ = -1;

    
    virtual ISegmentReaderBase* GetCurrentSegmentReader() const = 0;
    virtual void ResetCurrentSegmentReader() = 0;
    virtual void CreateCurrentSegmentReader() = 0;

    const NProto::TSegmentMeta& CurrentSegmentMeta() const;

    int FindSegmentByRowIndex(i64 rowIndex) const;
    i64 GetSegmentStartRowIndex(int segmentIndex) const;

    int FindFirstBlockSegment() const;
    int FindLastBlockSegment() const;

    void ResetCurrentSegmentReaderOnEos();
    void EnsureCurrentSegmentReader();
    void RearmSegmentReader();
};

////////////////////////////////////////////////////////////////////////////////

class TUnversionedColumnReaderBase
    : public TColumnReaderBase
    , public IUnversionedColumnReader
{
public:
    TUnversionedColumnReaderBase(
        const NProto::TColumnMeta& columnMeta,
        int columnIndex,
        int columnId);

    virtual void ReadValues(TMutableRange<NTableClient::TMutableVersionedRow> rows) override;
    virtual void ReadValues(TMutableRange<NTableClient::TMutableUnversionedRow> rows) override;

    virtual int GetBatchColumnCount() override;
    virtual void ReadColumnarBatch(
        TMutableRange<NTableClient::IUnversionedRowBatch::TColumn> columns,
        i64 rowCount) override;
    virtual i64 EstimateDataWeight(i64 lowerRowIndex, i64 upperRowIndex) override;

protected:
    const int ColumnIndex_;
    const int ColumnId_;

    std::unique_ptr<IUnversionedSegmentReader> SegmentReader_;

    
    virtual ISegmentReaderBase* GetCurrentSegmentReader() const override;
    virtual void ResetCurrentSegmentReader() override;
    virtual void CreateCurrentSegmentReader() override;

    virtual std::unique_ptr<IUnversionedSegmentReader> CreateSegmentReader(
        int segmentIndex,
        bool scan = true) = 0;

    template <class TSegmentReader>
    std::unique_ptr<IUnversionedSegmentReader> DoCreateSegmentReader(const NProto::TSegmentMeta& meta)
    {
        IUnversionedSegmentReader* reader = new TSegmentReader(
            TRef(Block_.Begin() + meta.offset(), meta.size()),
            meta,
            ColumnIndex_,
            ColumnId_);
        return std::unique_ptr<IUnversionedSegmentReader>(reader);
    }

    template <class TRow>
    void DoReadValues(TMutableRange<TRow> rows)
    {
        i64 readRowCount = 0;
        while (readRowCount < rows.Size()) {
            RearmSegmentReader();
            i64 count = SegmentReader_->ReadValues(rows.Slice(rows.Begin() + readRowCount, rows.End()));
            readRowCount += count;
            CurrentRowIndex_ += count;
        }
    }

    template <NTableClient::EValueType ValueType>
    std::pair<i64, i64> DoGetEqualRange(
        const NTableClient::TUnversionedValue& value,
        i64 lowerRowIndex,
        i64 upperRowIndex)
    {
        // Use lookup segment readers while GetEqualRange.
        YT_VERIFY(lowerRowIndex <= upperRowIndex);

        if (lowerRowIndex == upperRowIndex) {
            return std::make_pair(lowerRowIndex, upperRowIndex);
        }

        int segmentLimit = FindSegmentByRowIndex(upperRowIndex - 1);
        segmentLimit = std::min(segmentLimit, LastBlockSegmentIndex_);

        // Get lower limit for range.
        int lowerSegmentIndex = FindSegmentByRowIndex(lowerRowIndex);
        auto lowerSegmentReader = CreateSegmentReader(lowerSegmentIndex, false);

        while (lowerSegmentIndex < segmentLimit &&
            CompareValues<ValueType>(lowerSegmentReader->GetLastValue(), value) < 0)
        {
            lowerSegmentReader = CreateSegmentReader(++lowerSegmentIndex, false);
        }

        if (lowerRowIndex > GetSegmentStartRowIndex(lowerSegmentIndex)) {
            lowerSegmentReader->SkipToRowIndex(lowerRowIndex);
        }

        lowerRowIndex = lowerSegmentReader->GetLowerRowIndex(value, upperRowIndex);

        // Get upper limit for range.
        int upperSegmentIndex = lowerSegmentIndex;
        auto upperSegmentReader = CreateSegmentReader(upperSegmentIndex, false);

        while (upperSegmentIndex < segmentLimit &&
            CompareValues<ValueType>(upperSegmentReader->GetLastValue(), value) <= 0)
        {
            upperSegmentReader = CreateSegmentReader(++upperSegmentIndex, false);
        }

        if (lowerRowIndex > GetSegmentStartRowIndex(upperSegmentIndex)) {
            upperSegmentReader->SkipToRowIndex(lowerRowIndex);
        }

        upperRowIndex = upperSegmentReader->GetUpperRowIndex(value, upperRowIndex);

        return std::make_pair(lowerRowIndex, upperRowIndex);
    }
};

////////////////////////////////////////////////////////////////////////////////

class TVersionedValueExtractorBase
{
public:
    explicit TVersionedValueExtractorBase(bool aggregate);

    ui32 GetTimestampIndex(i64 valueIndex) const;

    bool GetAggregate(i64 valueIndex) const;

protected:
    const bool Aggregate_;

    TBitPackedUnsignedVectorReader<ui32> TimestampIndexReader_;
    TReadOnlyBitmap<ui64> AggregateBitmap_;

    
    const char* InitTimestampIndexReader(const char* ptr);
};

////////////////////////////////////////////////////////////////////////////////

class TDenseVersionedValueExtractorBase
    : public TVersionedValueExtractorBase
{
public:
    TDenseVersionedValueExtractorBase(const NProto::TSegmentMeta& meta, bool aggregate);

    std::pair<ui32, ui32> GetValueIndexRange(i64 segmentRowIndex, ui32 lowerTimestampIndex);

    // For compaction read.
    std::pair<ui32, ui32> GetValueIndexRange(i64 segmentRowIndex);

    i64 GetLowerValueIndex(i64 segmentRowIndex) const;

    ui32 GetValueCount(i64 segmentRowIndex) const;

protected:
    const char* InitDenseReader(const char* ptr);

private:
    const NProto::TDenseVersionedSegmentMeta& DenseVersionedMeta_;
    TBitPackedUnsignedVectorReader<ui32> ValuesPerRowDiffReader_;
};

////////////////////////////////////////////////////////////////////////////////

class TSparseVersionedValueExtractorBase
    : public TVersionedValueExtractorBase
{
public:
    TSparseVersionedValueExtractorBase(
        const NProto::TSegmentMeta& meta,
        bool aggregate);

    i64 GetLowerValueIndex(i64 segmentRowIndex, int valueIndex) const;

    i64 GetRowIndex(i64 valueIndex) const;

    i64 GetValueCount() const;

    std::pair<ui32, ui32> GetValueIndexRange(i64 segmentRowIndex, i64 valueIndex, ui32 lowerTimestampIndex);
    std::pair<ui32, ui32> GetValueIndexRange(i64 segmentRowIndex, i64 valueIndex);

protected:
    const char* InitSparseReader(const char* ptr);

private:
    TBitPackedUnsignedVectorReader<ui64> RowIndexReader_;
};

////////////////////////////////////////////////////////////////////////////////

template <class TValueExtractor>
class TVersionedSegmentReaderBase
    : public IVersionedSegmentReader
{
public:
    TVersionedSegmentReaderBase(
        TRef data,
        const NProto::TSegmentMeta& meta,
        int columnId,
        bool aggregate)
        : Data_(data)
        , Meta_(meta)
        , Aggregate_(aggregate)
        , ColumnId_(columnId)
        , SegmentStartRowIndex_(meta.chunk_row_count() - meta.row_count())
        , ValueExtractor_(data, meta, aggregate)
    { }

protected:
    const TRef Data_;
    const NProto::TSegmentMeta& Meta_;
    const bool Aggregate_;
    const int ColumnId_;

    const i64 SegmentStartRowIndex_;

    i64 SegmentRowIndex_ = 0;

    TValueExtractor ValueExtractor_;


    i64 GetSegmentRowIndex(i64 rowIndex) const
    {
        return rowIndex - SegmentStartRowIndex_;
    }

    void DoSetValues(
        NTableClient::TMutableVersionedRow row,
        const std::pair<ui32, ui32>& timestampIndexRange,
        const std::pair<ui32, ui32>& valueIndexRange,
        bool produceAllVersions)
    {
        ui32 valueIndex = valueIndexRange.first;
        ui32 upperValueIndex = valueIndexRange.second;
        for (; valueIndex < upperValueIndex; ++valueIndex) {
            ui32 timestampIndex = ValueExtractor_.GetTimestampIndex(valueIndex);
            if (timestampIndex >= timestampIndexRange.second) {
                // Value in given timestamp range doesn't exist.
                return;
            }

            auto* value = row.BeginValues() + row.GetValueCount();
            row.SetValueCount(row.GetValueCount() + 1);
            value->Timestamp = timestampIndex;

            bool aggregate = ValueExtractor_.GetAggregate(valueIndex);
            ValueExtractor_.ExtractValue(value, valueIndex, ColumnId_, aggregate);

            if (!produceAllVersions && !Aggregate_) {
                break;
            }
        }
    }

    void DoSetAllValues(
        NTableClient::TMutableVersionedRow row,
        const std::pair<ui32, ui32>& valueIndexRange)
    {
        ui32 valueIndex = valueIndexRange.first;
        ui32 upperValueIndex = valueIndexRange.second;
        for (; valueIndex < upperValueIndex; ++valueIndex) {
            auto* value = row.BeginValues() + row.GetValueCount();
            row.SetValueCount(row.GetValueCount() + 1);
            value->Timestamp = ValueExtractor_.GetTimestampIndex(valueIndex);
            bool aggregate = ValueExtractor_.GetAggregate(valueIndex);
            ValueExtractor_.ExtractValue(value, valueIndex, ColumnId_, aggregate);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

template <class TValueExtractor>
class TDenseVersionedSegmentReader
    : public TVersionedSegmentReaderBase<TValueExtractor>
{
public:
    TDenseVersionedSegmentReader(
        TRef data,
        const NProto::TSegmentMeta& meta,
        int columnId,
        bool aggregate)
        : TVersionedSegmentReaderBase<TValueExtractor>(data, meta, columnId, aggregate)
    { }

    virtual void SkipToRowIndex(i64 rowIndex) override
    {
        YT_VERIFY(GetSegmentRowIndex(rowIndex) >= SegmentRowIndex_);
        SegmentRowIndex_ = GetSegmentRowIndex(rowIndex);
    }

    virtual i64 ReadValues(
        TMutableRange<NTableClient::TMutableVersionedRow> rows,
        TRange<std::pair<ui32, ui32>> timestampIndexRanges,
        bool produceAllVersions) override
    {
        YT_VERIFY(rows.Size() == timestampIndexRanges.Size());

        i64 rangeRowIndex = 0;
        while (rangeRowIndex < rows.Size() && SegmentRowIndex_ < Meta_.row_count()) {
            auto row = rows[rangeRowIndex];
            if (row) {
                SetValues(row, timestampIndexRanges[rangeRowIndex], produceAllVersions);
            }

            ++SegmentRowIndex_;
            ++rangeRowIndex;
        }
        return rangeRowIndex;
    }

    virtual i64 ReadAllValues(TMutableRange<NTableClient::TMutableVersionedRow> rows) override
    {
        i64 rangeRowIndex = 0;
        while (rangeRowIndex < rows.Size() && SegmentRowIndex_ < Meta_.row_count()) {
            auto row = rows[rangeRowIndex];
            YT_VERIFY(row);
            SetAllValues(row);

            ++SegmentRowIndex_;
            ++rangeRowIndex;
        }
        return rangeRowIndex;
    }

    void ReadValueCounts(TMutableRange<ui32> valueCounts) const
    {
        YT_VERIFY(SegmentRowIndex_ + valueCounts.Size() <= Meta_.row_count());

        for (i64 rangeRowIndex = 0; rangeRowIndex < valueCounts.Size(); ++rangeRowIndex) {
            valueCounts[rangeRowIndex] = ValueExtractor_.GetValueCount(SegmentRowIndex_ + rangeRowIndex);
        }
    }

private:
    using TVersionedSegmentReaderBase<TValueExtractor>::GetSegmentRowIndex;
    using TVersionedSegmentReaderBase<TValueExtractor>::ValueExtractor_;
    using TVersionedSegmentReaderBase<TValueExtractor>::SegmentRowIndex_;
    using TVersionedSegmentReaderBase<TValueExtractor>::Meta_;
    using TVersionedSegmentReaderBase<TValueExtractor>::DoSetValues;
    using TVersionedSegmentReaderBase<TValueExtractor>::DoSetAllValues;

    
    void SetValues(
        NTableClient::TMutableVersionedRow row,
        const std::pair<ui32, ui32>& timestampIndexRange,
        bool produceAllVersions)
    {
        auto valueIndexRange = ValueExtractor_.GetValueIndexRange(
            SegmentRowIndex_,
            timestampIndexRange.first);

        DoSetValues(row, timestampIndexRange, valueIndexRange, produceAllVersions);
    }

    void SetAllValues(NTableClient::TMutableVersionedRow row)
    {
        auto valueIndexRange = ValueExtractor_.GetValueIndexRange(SegmentRowIndex_);
        DoSetAllValues(row, valueIndexRange);
    }
};

////////////////////////////////////////////////////////////////////////////////

template <class TValueExtractor>
class TSparseVersionedSegmentReader
    : public TVersionedSegmentReaderBase<TValueExtractor>
{
public:
    TSparseVersionedSegmentReader(
        TRef data,
        const NProto::TSegmentMeta& meta,
        int columnId,
        bool aggregate)
        : TVersionedSegmentReaderBase<TValueExtractor>(data, meta, columnId, aggregate)
    { }

    virtual void SkipToRowIndex(i64 rowIndex) override
    {
        YT_VERIFY(GetSegmentRowIndex(rowIndex) >= SegmentRowIndex_);

        if (GetSegmentRowIndex(rowIndex) > SegmentRowIndex_) {
            SegmentRowIndex_ = GetSegmentRowIndex(rowIndex);

            ValueIndex_ = ValueExtractor_.GetLowerValueIndex(SegmentRowIndex_, ValueIndex_);
        }
    }

    virtual i64 ReadValues(
        TMutableRange<NTableClient::TMutableVersionedRow> rows,
        TRange<std::pair<ui32, ui32>> timestampIndexRanges,
        bool produceAllVersions) override
    {
        YT_VERIFY(rows.Size() == timestampIndexRanges.Size());

        i64 rangeRowIndex = 0;
        while (rangeRowIndex < rows.Size() && SegmentRowIndex_ < Meta_.row_count()) {
            if (ValueIndex_ == ValueExtractor_.GetValueCount()) {
                // We reached the last value in the segment, left rows are empty.
                i64 rowsToSkip = std::min(
                    static_cast<i64>(rows.Size() - rangeRowIndex),
                    Meta_.row_count() - SegmentRowIndex_);

                SegmentRowIndex_ += rowsToSkip;
                rangeRowIndex += rowsToSkip;
                break;
            }

            if (SegmentRowIndex_ < ValueExtractor_.GetRowIndex(ValueIndex_)) {
                // Skip rows up to index of current value.
                i64 rowsToSkip = std::min(
                    static_cast<i64>(rows.Size() - rangeRowIndex),
                    ValueExtractor_.GetRowIndex(ValueIndex_) - SegmentRowIndex_);

                SegmentRowIndex_ += rowsToSkip;
                rangeRowIndex += rowsToSkip;
                continue;
            }

            YT_VERIFY(SegmentRowIndex_ == ValueExtractor_.GetRowIndex(ValueIndex_));

            auto row = rows[rangeRowIndex];
            if (row){
                SetValues(row, timestampIndexRanges[rangeRowIndex], produceAllVersions);
            }

            ++SegmentRowIndex_;
            ++rangeRowIndex;

            ValueIndex_ = ValueExtractor_.GetLowerValueIndex(SegmentRowIndex_, ValueIndex_);
        }
        return rangeRowIndex;
    }

    virtual i64 ReadAllValues(TMutableRange<NTableClient::TMutableVersionedRow> rows) override
    {
        i64 rangeRowIndex = 0;
        while (rangeRowIndex < rows.Size() && SegmentRowIndex_ < Meta_.row_count()) {
            if (ValueIndex_ == ValueExtractor_.GetValueCount()) {
                // We reached the last value in the segment, left rows are empty.
                i64 rowsToSkip = std::min(
                    static_cast<i64>(rows.Size() - rangeRowIndex),
                    Meta_.row_count() - SegmentRowIndex_);

                SegmentRowIndex_ += rowsToSkip;
                rangeRowIndex += rowsToSkip;
                break;
            }

            if (SegmentRowIndex_ < ValueExtractor_.GetRowIndex(ValueIndex_)) {
                // Skip rows up to index of current value.
                i64 rowsToSkip = std::min(
                    static_cast<i64>(rows.Size() - rangeRowIndex),
                    ValueExtractor_.GetRowIndex(ValueIndex_) - SegmentRowIndex_);

                SegmentRowIndex_ += rowsToSkip;
                rangeRowIndex += rowsToSkip;
                continue;
            }

            YT_VERIFY(SegmentRowIndex_ == ValueExtractor_.GetRowIndex(ValueIndex_));

            auto row = rows[rangeRowIndex];
            SetAllValues(row);

            ++SegmentRowIndex_;
            ++rangeRowIndex;

            ValueIndex_ = ValueExtractor_.GetLowerValueIndex(SegmentRowIndex_, ValueIndex_);
        }
        return rangeRowIndex;
    }

    virtual void ReadValueCounts(TMutableRange<ui32> valueCounts) const override
    {
        YT_VERIFY(SegmentRowIndex_ + valueCounts.Size() <= Meta_.row_count());

        i64 rangeRowIndex = 0;
        i64 currentValueIndex = ValueIndex_;
        i64 currentRowIndex = SegmentRowIndex_;
        while (rangeRowIndex < valueCounts.Size()) {
            if (currentValueIndex == ValueExtractor_.GetValueCount()) {
                // We reached the last value in the segment, left rows are empty.
                for (; rangeRowIndex < valueCounts.Size(); ++rangeRowIndex) {
                    valueCounts[rangeRowIndex] = 0;
                }
                break;
            }

            if (currentRowIndex < ValueExtractor_.GetRowIndex(currentValueIndex)) {
                // Skip rows up to index of current value.
                for (;
                    currentRowIndex < ValueExtractor_.GetRowIndex(currentValueIndex) &&
                    rangeRowIndex < valueCounts.Size();
                    ++rangeRowIndex, ++currentRowIndex)
                {
                    valueCounts[rangeRowIndex] = 0;
                }
                continue;
            }

            YT_VERIFY(currentRowIndex == ValueExtractor_.GetRowIndex(currentValueIndex));
            ui32 count = 0;
            while (currentValueIndex < ValueExtractor_.GetValueCount() &&
                currentRowIndex == ValueExtractor_.GetRowIndex(currentValueIndex))
            {
                ++count;
                ++currentValueIndex;
            }

            valueCounts[rangeRowIndex] = count;
            ++rangeRowIndex;
            ++currentRowIndex;
        }
    }

private:
    i64 ValueIndex_ = 0;

    using TVersionedSegmentReaderBase<TValueExtractor>::GetSegmentRowIndex;
    using TVersionedSegmentReaderBase<TValueExtractor>::ValueExtractor_;
    using TVersionedSegmentReaderBase<TValueExtractor>::SegmentRowIndex_;
    using TVersionedSegmentReaderBase<TValueExtractor>::Meta_;
    using TVersionedSegmentReaderBase<TValueExtractor>::DoSetValues;
    using TVersionedSegmentReaderBase<TValueExtractor>::DoSetAllValues;

    
    void SetValues(
        NTableClient::TMutableVersionedRow row,
        std::pair<ui32, ui32> timestampIndexRange,
        bool produceAllVersions)
    {
        auto valueIndexRange = ValueExtractor_.GetValueIndexRange(
            SegmentRowIndex_,
            ValueIndex_,
            timestampIndexRange.first);

        DoSetValues(row, timestampIndexRange, valueIndexRange, produceAllVersions);
    }

    void SetAllValues(NTableClient::TMutableVersionedRow row)
    {
        auto valueIndexRange = ValueExtractor_.GetValueIndexRange(
            SegmentRowIndex_,
            ValueIndex_);

        DoSetAllValues(row, valueIndexRange);
    }
};

////////////////////////////////////////////////////////////////////////////////

class TVersionedColumnReaderBase
    : public IVersionedColumnReader
    , public TColumnReaderBase
{
public:
    TVersionedColumnReaderBase(
        const NProto::TColumnMeta& columnMeta,
        int columnId,
        bool aggregate);

    virtual void ReadValueCounts(TMutableRange<ui32> valueCounts) override;

    virtual void ReadValues(
        TMutableRange<NTableClient::TMutableVersionedRow> rows,
        TRange<std::pair<ui32, ui32>> timestampIndexRanges,
        bool produceAllVersions) override;

    virtual void ReadAllValues(TMutableRange<NTableClient::TMutableVersionedRow> rows) override;

protected:
    const int ColumnId_;
    const bool Aggregate_;

    std::unique_ptr<IVersionedSegmentReader> SegmentReader_;

    
    virtual ISegmentReaderBase* GetCurrentSegmentReader() const override;
    virtual void ResetCurrentSegmentReader() override;
    virtual void CreateCurrentSegmentReader() override;

    virtual std::unique_ptr<IVersionedSegmentReader> CreateSegmentReader(int segmentIndex) = 0;

    template <class TSegmentReader>
    std::unique_ptr<IVersionedSegmentReader> DoCreateSegmentReader(const NProto::TSegmentMeta& meta)
    {
        const char* segmentBegin = Block_.Begin() + meta.offset();
        return std::make_unique<TSegmentReader>(
            TRef(segmentBegin, segmentBegin + meta.size()),
            meta,
            ColumnId_,
            Aggregate_);
    }
};

////////////////////////////////////////////////////////////////////////////////

void ReadColumnarNullBitmap(
    NTableClient::IUnversionedRowBatch::TColumn* column,
    i64 startIndex,
    i64 valueCount,
    TRange<ui64> bitmap);

void ReadColumnarIntegerValues(
    NTableClient::IUnversionedRowBatch::TColumn* column,
    i64 startIndex,
    i64 valueCount,
    NTableClient::EValueType valueType,
    ui64 baseValue,
    TRange<ui64> data);

void ReadColumnarBooleanValues(
    NTableClient::IUnversionedRowBatch::TColumn* column,
    i64 startIndex,
    i64 valueCount,
    TRange<ui64> bitmap);

void ReadColumnarDoubleValues(
    NTableClient::IUnversionedRowBatch::TColumn* column,
    i64 startIndex,
    i64 valueCount,
    TRange<double> data);

void ReadColumnarStringValues(
    NTableClient::IUnversionedRowBatch::TColumn* column,
    i64 startIndex,
    i64 valueCount,
    ui32 avgLength,
    TRange<ui32> offsets,
    TRef stringData);

void ReadColumnarDictionary(
    NTableClient::IUnversionedRowBatch::TColumn* primaryColumn,
    NTableClient::IUnversionedRowBatch::TColumn* dictionaryColumn,
    NTableClient::TLogicalTypePtr type,
    i64 startIndex,
    i64 valueCount,
    TRange<ui32> ids);

void ReadColumnarRle(
    NTableClient::IUnversionedRowBatch::TColumn* primaryColumn,
    NTableClient::IUnversionedRowBatch::TColumn* rleColumn,
    NTableClient::TLogicalTypePtr type,
    i64 startIndex,
    i64 valueCount,
    TRange<ui64> indexes);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableChunkFormat
