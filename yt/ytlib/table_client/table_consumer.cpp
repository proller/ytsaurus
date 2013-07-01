﻿#include "stdafx.h"
#include "sync_writer.h"
#include "table_consumer.h"
#include "config.h"

#include <ytlib/misc/string.h>

namespace NYT {
namespace NTableClient {

using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

TTableConsumer::TTableConsumer(IWriterBasePtr writer)
    : ControlState(EControlState::None)
    , CurrentTableIndex(0)
    , Writer(writer)
    , Depth(0)
    , ValueWriter(&RowBuffer)
{
    Writers.push_back(writer);
}

TTableConsumer::TTableConsumer(const std::vector<IWriterBasePtr>& writers, int tableIndex)
    : ControlState(EControlState::None)
    , CurrentTableIndex(tableIndex)
    , Writers(std::move(writers))
    , Writer(Writers[CurrentTableIndex])
    , Depth(0)
    , ValueWriter(&RowBuffer)
{ }

void TTableConsumer::OnStringScalar(const TStringBuf& value)
{
    if (ControlState == EControlState::ExpectValue) {
        YASSERT(Depth == 1);
        ThrowInvalidControlAttribute("be a string value");
    }

    YASSERT(ControlState == EControlState::None);

    if (Depth == 0) {
        ThrowMapExpected();
    } else {
        ValueWriter.OnStringScalar(value);
    }
}

void TTableConsumer::OnIntegerScalar(i64 value)
{
    if (ControlState == EControlState::ExpectValue) {
        YASSERT(Depth == 1);

        switch (ControlAttribute) {
            case EControlAttribute::TableIndex: {
                if (value < 0 || value >= Writers.size()) {
                    THROW_ERROR_EXCEPTION(
                        "Invalid table index: expected in range [0, %d], actual %" PRId64,
                        static_cast<int>(Writers.size()) - 1,
                        value)
                        << TErrorAttribute("row_index", Writer->GetRowCount());
                }
                CurrentTableIndex = value;
                Writer = Writers[CurrentTableIndex];
                ControlState = EControlState::ExpectEndAttributes;
                break;
            }

            default:
                ThrowInvalidControlAttribute("be an integer value");
        }

        return;
    }

    YASSERT(ControlState == EControlState::None);

    if (Depth == 0) {
        ThrowMapExpected();
    } else {
        ValueWriter.OnIntegerScalar(value);
    }
}

void TTableConsumer::OnDoubleScalar(double value)
{
    if (ControlState == EControlState::ExpectValue) {
        YASSERT(Depth == 1);
        ThrowInvalidControlAttribute("be a double value");
    }

    YASSERT(ControlState == EControlState::None);

    if (Depth == 0) {
        ThrowMapExpected();
    } else {
        ValueWriter.OnDoubleScalar(value);
    }
}

void TTableConsumer::OnEntity()
{
    switch (ControlState) {
    case EControlState::None:
        break;

    case EControlState::ExpectEntity:
        YASSERT(Depth == 0);
        // Successfully processed control statement.
        ControlState = EControlState::None;
        return;

    case EControlState::ExpectValue:
        ThrowInvalidControlAttribute("be an entity");
        break;

    default:
        YUNREACHABLE();
    };


    if (Depth == 0) {
        ThrowMapExpected();
    } else {
        ValueWriter.OnEntity();
    }
}

void TTableConsumer::OnBeginList()
{
    if (ControlState == EControlState::ExpectValue) {
        YASSERT(Depth == 1);
        ThrowInvalidControlAttribute("be a list");
    }

    YASSERT(ControlState == EControlState::None);

    if (Depth == 0) {
        ThrowMapExpected();
    } else {
        ++Depth;
        ValueWriter.OnBeginList();
    }
}

void TTableConsumer::OnBeginAttributes()
{
    if (ControlState == EControlState::ExpectValue) {
        YASSERT(Depth == 1);
        ThrowInvalidControlAttribute("have attributes");
    }

    YASSERT(ControlState == EControlState::None);

    if (Depth == 0) {
        ControlState = EControlState::ExpectName;
    } else {
        ValueWriter.OnBeginAttributes();
    }

    ++Depth;
}

void TTableConsumer::ThrowMapExpected()
{
    THROW_ERROR_EXCEPTION("Invalid row format, map expected")
        << TErrorAttribute("table_index", CurrentTableIndex)
        << TErrorAttribute("row_index", Writer->GetRowCount());
}

void TTableConsumer::ThrowInvalidControlAttribute(const Stroka& whatsWrong)
{
    THROW_ERROR_EXCEPTION("Control attribute %s cannot %s",
        ~FormatEnum(ControlAttribute).Quote(),
        ~whatsWrong)
        << TErrorAttribute("table_index", CurrentTableIndex)
        << TErrorAttribute("row_index", Writer->GetRowCount());
}

void TTableConsumer::OnListItem()
{
    YASSERT(ControlState == EControlState::None);

    if (Depth == 0) {
        // Row separator, do nothing.
    } else {
        ValueWriter.OnListItem();
    }
}

void TTableConsumer::OnBeginMap()
{
    if (ControlState == EControlState::ExpectValue) {
        YASSERT(Depth == 1);
        ThrowInvalidControlAttribute("be a map");
    }

    YASSERT(ControlState == EControlState::None);

    if (Depth > 0) {
        ValueWriter.OnBeginMap();
    }

    ++Depth;
}

void TTableConsumer::OnKeyedItem(const TStringBuf& name)
{
    switch (ControlState) {
    case EControlState::None:
        break;

    case EControlState::ExpectName:
        YASSERT(Depth == 1);
        try {
            ControlAttribute = ParseEnum<EControlAttribute>(ToString(name));
        } catch (const std::exception&) {
            // Ignore ex, our custom message is more meaningful.
            THROW_ERROR_EXCEPTION("Failed to parse control attribute name %s",
                ~Stroka(name).Quote());
        }
        ControlState = EControlState::ExpectValue;
        return;

    case EControlState::ExpectEndAttributes:
        YASSERT(Depth == 1);
        THROW_ERROR_EXCEPTION("Too many control attributes per record: at most one attribute is allowed");
        break;

    default:
        YUNREACHABLE();

    };

    YASSERT(Depth > 0);
    if (Depth == 1) {
        Offsets.push_back(RowBuffer.Size());
        RowBuffer.Write(name);

        Offsets.push_back(RowBuffer.Size());
    } else {
        ValueWriter.OnKeyedItem(name);
    }
}

void TTableConsumer::OnEndMap()
{
    YASSERT(Depth > 0);
    // No control attribute allows map or composite values.
    YASSERT(ControlState == EControlState::None);

    --Depth;

    if (Depth > 0) {
        ValueWriter.OnEndMap();
        return;
    }

    YASSERT(Offsets.size() % 2 == 0);

    TRow row;
    row.reserve(Offsets.size() / 2);

    int index = Offsets.size();
    int begin = RowBuffer.Size();
    while (index > 0) {
        int end = begin;
        begin = Offsets[--index];
        TStringBuf value(RowBuffer.Begin() + begin, end - begin);

        end = begin;
        begin = Offsets[--index];
        TStringBuf name(RowBuffer.Begin() + begin, end - begin);

        row.push_back(std::make_pair(name, value));
    }

    Writer->WriteRow(row);

    Offsets.clear();
    RowBuffer.Clear();
}

void TTableConsumer::OnEndList()
{
   // No control attribute allow list or composite values.
    YASSERT(ControlState == EControlState::None);

    --Depth;
    YASSERT(Depth > 0);
    ValueWriter.OnEndList();
}

void TTableConsumer::OnEndAttributes()
{
    --Depth;

    switch (ControlState) {
        case EControlState::ExpectName:
            THROW_ERROR_EXCEPTION("Too few control attributes per record: at least one attribute is required");
            break;

        case EControlState::ExpectEndAttributes:
            YASSERT(Depth == 0);
            ControlState = EControlState::ExpectEntity;
            break;

        case EControlState::None:
            YASSERT(Depth > 0);
            ValueWriter.OnEndAttributes();
            break;

        default:
            YUNREACHABLE();
    };
}

void TTableConsumer::OnRaw(const TStringBuf& yson, EYsonType type)
{
    YUNREACHABLE();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
