#include "writer.h"
#include "detail.h"
#include "format.h"

#include <yt/core/misc/varint.h>

namespace NYT {
namespace NYson {

////////////////////////////////////////////////////////////////////////////////

// Copied from <util/string/escape.cpp>
namespace {

static inline char HexDigit(char value) {
    YASSERT(value < 16);
    if (value < 10)
        return '0' + value;
    else
        return 'A' + value - 10;
}

static inline char OctDigit(char value) {
    YASSERT(value < 8);
    return '0' + value;
}

static inline bool IsPrintable(char c) {
    return c >= 32 && c <= 126;
}

static inline bool IsHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

static inline bool IsOctDigit(char c) {
    return  c >= '0' && c <= '7';
}

static const size_t ESCAPE_C_BUFFER_SIZE = 4;

static inline size_t EscapeC(unsigned char c, char next, char r[ESCAPE_C_BUFFER_SIZE]) {
    // (1) Printable characters go as-is, except backslash and double quote.
    // (2) Characters \r, \n, \t and \0 ... \7 replaced by their simple escape characters (if possible).
    // (3) Otherwise, character is encoded using hexadecimal escape sequence (if possible), or octal.
    if (c == '\"') {
        r[0] = '\\';
        r[1] = '\"';
        return 2;
    } else if (c == '\\') {
        r[0] = '\\';
        r[1] = '\\';
        return 2;
    } else if (IsPrintable(c)) {
        r[0] = c;
        return 1;
    } else if (c == '\r') {
        r[0] = '\\';
        r[1] = 'r';
        return 2;
    } else if (c == '\n') {
        r[0] = '\\';
        r[1] = 'n';
        return 2;
    } else if (c == '\t') {
        r[0] = '\\';
        r[1] = 't';
        return 2;
   } else if (c < 8 && !IsOctDigit(next)) {
        r[0] = '\\';
        r[1] = OctDigit(c);
        return 2;
    } else if (!IsHexDigit(next)) {
        r[0] = '\\';
        r[1] = 'x';
        r[2] = HexDigit((c & 0xF0) >> 4);
        r[3] = HexDigit((c & 0x0F) >> 0);
        return 4;
    } else {
        r[0] = '\\';
        r[1] = OctDigit((c & 0700) >> 6);
        r[2] = OctDigit((c & 0070) >> 3);
        r[3] = OctDigit((c & 0007) >> 0);
        return 4;
    }
}

void EscapeC(const char* str, size_t len, TOutputStream& output) {
    char buffer[ESCAPE_C_BUFFER_SIZE];

    size_t i, j;
    for (i = 0, j = 0; i < len; ++i) {
        size_t rlen = EscapeC(str[i], (i + 1 < len ? str[i + 1] : 0), buffer);

        if (rlen > 1) {
            output.Write(str + j, i - j);
            j = i + 1;
            output.Write(buffer, rlen);
        }
    }

    if (j > 0) {
        output.Write(str + j, len - j);
    } else {
        output.Write(str, len);
    }
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

TYsonWriter::TYsonWriter(
    TOutputStream* stream,
    EYsonFormat format,
    EYsonType type,
    bool enableRaw,
    bool booleanAsString,
    int indent)
    : Stream_(stream)
    , Format_(format)
    , Type_(type)
    , EnableRaw_(enableRaw)
    , BooleanAsString_(booleanAsString)
    , IndentSize_(indent)
{
    YASSERT(Stream_);
}

void TYsonWriter::WriteIndent()
{
    for (int i = 0; i < IndentSize_ * Depth_; ++i) {
        Stream_->Write(' ');
    }
}

void TYsonWriter::EndNode()
{
    if (Depth_ > 0 || Type_ != EYsonType::Node) {
        Stream_->Write(NDetail::ItemSeparatorSymbol);
        if ((Depth_ > 0 && Format_ == EYsonFormat::Pretty) ||
            (Depth_ == 0 && Format_ != EYsonFormat::Binary))
        {
            Stream_->Write('\n');
        }
    }
}

void TYsonWriter::BeginCollection(char ch)
{
    ++Depth_;
    EmptyCollection_ = true;
    Stream_->Write(ch);
}

void TYsonWriter::CollectionItem()
{
    if (Format_ == EYsonFormat::Pretty) {
        if (EmptyCollection_ && Depth_ > 0) {
            Stream_->Write('\n');
        }
        WriteIndent();
    }
    EmptyCollection_ = false;
}

void TYsonWriter::EndCollection(char ch)
{
    --Depth_;
    if (Format_ == EYsonFormat::Pretty && !EmptyCollection_) {
        WriteIndent();
    }
    EmptyCollection_ = false;
    Stream_->Write(ch);
}

void TYsonWriter::WriteStringScalar(const TStringBuf& value)
{
    if (Format_ == EYsonFormat::Binary) {
        Stream_->Write(NDetail::StringMarker);
        WriteVarInt32(Stream_, static_cast<i32>(value.length()));
        Stream_->Write(value.begin(), value.length());
    } else {
        Stream_->Write('"');
        EscapeC(value.data(), value.length(), *Stream_);
        Stream_->Write('"');
    }
}

void TYsonWriter::OnStringScalar(const TStringBuf& value)
{
    WriteStringScalar(value);
    EndNode();
}

void TYsonWriter::OnInt64Scalar(i64 value)
{
    if (Format_ == EYsonFormat::Binary) {
        Stream_->Write(NDetail::Int64Marker);
        WriteVarInt64(Stream_, value);
    } else {
        Stream_->Write(::ToString(value));
    }
    EndNode();
}

void TYsonWriter::OnUint64Scalar(ui64 value)
{
    if (Format_ == EYsonFormat::Binary) {
        Stream_->Write(NDetail::Uint64Marker);
        WriteVarUint64(Stream_, value);
    } else {
        Stream_->Write(::ToString(value));
        Stream_->Write("u");
    }
    EndNode();
}

void TYsonWriter::OnDoubleScalar(double value)
{
    if (Format_ == EYsonFormat::Binary) {
        Stream_->Write(NDetail::DoubleMarker);
        Stream_->Write(&value, sizeof(double));
    } else {
        char buf[256];
        auto str = TStringBuf(buf, FloatToString(value, buf, sizeof(buf)));
        Stream_->Write(str);
        if (str.find('.') == Stroka::npos && str.find('e') == Stroka::npos) {
            Stream_->Write(".");
        }
    }
    EndNode();
}

void TYsonWriter::OnBooleanScalar(bool value)
{
    if (BooleanAsString_) {
        OnStringScalar(value ? STRINGBUF("true") : STRINGBUF("false"));
        return;
    }

    if (Format_ == EYsonFormat::Binary) {
        Stream_->Write(value ? NDetail::TrueMarker : NDetail::FalseMarker);
    } else {
        Stream_->Write(value ? STRINGBUF("%true") : STRINGBUF("%false"));
    }
    EndNode();
}

void TYsonWriter::OnEntity()
{
    Stream_->Write(NDetail::EntitySymbol);
    EndNode();
}

void TYsonWriter::OnBeginList()
{
    BeginCollection(NDetail::BeginListSymbol);
}

void TYsonWriter::OnListItem()
{
    CollectionItem();
}

void TYsonWriter::OnEndList()
{
    EndCollection(NDetail::EndListSymbol);
    EndNode();
}

void TYsonWriter::OnBeginMap()
{
    BeginCollection(NDetail::BeginMapSymbol);
}

void TYsonWriter::OnKeyedItem(const TStringBuf& key)
{
    CollectionItem();

    WriteStringScalar(key);

    if (Format_ == EYsonFormat::Pretty) {
        Stream_->Write(' ');
    }
    Stream_->Write(NDetail::KeyValueSeparatorSymbol);
    if (Format_ == EYsonFormat::Pretty) {
        Stream_->Write(' ');
    }
}

void TYsonWriter::OnEndMap()
{
    EndCollection(NDetail::EndMapSymbol);
    EndNode();
}

void TYsonWriter::OnBeginAttributes()
{
    BeginCollection(NDetail::BeginAttributesSymbol);
}

void TYsonWriter::OnEndAttributes()
{
    EndCollection(NDetail::EndAttributesSymbol);
    if (Format_ == EYsonFormat::Pretty) {
        Stream_->Write(' ');
    }
}

void TYsonWriter::OnRaw(const TStringBuf& yson, EYsonType type)
{
    if (EnableRaw_) {
        Stream_->Write(yson);
        if (type == EYsonType::Node) {
            EndNode();
        }
    } else {
        TYsonConsumerBase::OnRaw(yson, type);
    }
}

void TYsonWriter::Flush()
{ }

int TYsonWriter::GetDepth() const
{
    return Depth_;
}

////////////////////////////////////////////////////////////////////////////////

TBufferedBinaryYsonWriter::TBufferedBinaryYsonWriter(
    TOutputStream* stream,
    EYsonType type,
    bool enableRaw,
    bool booleanAsString)
    : Stream_(stream)
    , Type_(type)
    , EnableRaw_(enableRaw)
    , BooleanAsString_(booleanAsString)
    , BufferStart_(Buffer_)
    , BufferEnd_(Buffer_ + BufferSize)
    , BufferCursor_(BufferStart_)
{
    YASSERT(Stream_);
}

Y_FORCE_INLINE void TBufferedBinaryYsonWriter::WriteStringScalar(const TStringBuf& value)
{
    size_t length = value.length();
    if (length <= MaxSmallStringLength) {
        // NB: +3 is since we're obliged to leave at least two spare buffer positions.
        EnsureSpace(length + MaxVarInt32Size + 2);
        *BufferCursor_++ = NDetail::StringMarker;
        BufferCursor_ += WriteVarInt32(BufferCursor_, static_cast<i32>(length));
        ::memcpy(BufferCursor_, value.data(), length);
        BufferCursor_ += length;
    } else {
        EnsureSpace(MaxVarInt32Size + 1);
        *BufferCursor_++ = NDetail::StringMarker;
        BufferCursor_ += WriteVarInt32(BufferCursor_, static_cast<i32>(length));
        Flush();
        Stream_->Write(value.begin(), length);
    }
}

Y_FORCE_INLINE void TBufferedBinaryYsonWriter::BeginCollection(char ch)
{
    ++Depth_;
    *BufferCursor_++ = ch;    
}

Y_FORCE_INLINE void TBufferedBinaryYsonWriter::EndCollection(char ch)
{
    --Depth_;
    *BufferCursor_++ = ch;    
}

Y_FORCE_INLINE void TBufferedBinaryYsonWriter::EndNode()
{
    if (Y_LIKELY(Type_ != EYsonType::Node || Depth_ > 0)) {
        *BufferCursor_++ = NDetail::ItemSeparatorSymbol;
    }
    if (Y_LIKELY(Type_ != EYsonType::Node && Depth_ == 0)) {
        *BufferCursor_++ = '\n';
    }
}

void TBufferedBinaryYsonWriter::Flush()
{
    size_t length = BufferCursor_ - BufferStart_;
    if (length > 0) {
        YCHECK(length <= BufferSize);
        Stream_->Write(BufferStart_, length);
        BufferCursor_ = BufferStart_;
    }
}

Y_FORCE_INLINE void TBufferedBinaryYsonWriter::EnsureSpace(size_t space)
{
    if (Y_LIKELY(BufferCursor_ + space <= BufferEnd_)) {
        return;
    }

    YCHECK(space <= BufferSize);
    Flush();
}

void TBufferedBinaryYsonWriter::OnStringScalar(const TStringBuf& value)
{
    // NB: This call always leaves at least one spare position in buffer.
    WriteStringScalar(value);
    EndNode();
}

void TBufferedBinaryYsonWriter::OnInt64Scalar(i64 value)
{
    EnsureSpace(MaxVarInt64Size + 2);
    *BufferCursor_++ = NDetail::Int64Marker;
    BufferCursor_ += WriteVarInt64(BufferCursor_, value);
    EndNode();
}

void TBufferedBinaryYsonWriter::OnUint64Scalar(ui64 value)
{
    EnsureSpace(MaxVarUint64Size + 2);
    *BufferCursor_++ = NDetail::Uint64Marker;
    BufferCursor_ += WriteVarUint64(BufferCursor_, value);
    EndNode();
}

void TBufferedBinaryYsonWriter::OnDoubleScalar(double value)
{
    EnsureSpace(sizeof(double) + 2);
    *BufferCursor_++ = NDetail::DoubleMarker;
    *(reinterpret_cast<double*>(BufferCursor_)) = value;
    BufferCursor_ += sizeof(double);
    EndNode();
}

void TBufferedBinaryYsonWriter::OnBooleanScalar(bool value)
{
    if (Y_UNLIKELY(BooleanAsString_)) {
        OnStringScalar(value ? STRINGBUF("true") : STRINGBUF("false"));
    } else {
        EnsureSpace(2);
        *BufferCursor_++ = (value ? NDetail::TrueMarker : NDetail::FalseMarker);
        EndNode();
    }
}

void TBufferedBinaryYsonWriter::OnEntity()
{
    EnsureSpace(2);
    *BufferCursor_++ = NDetail::EntitySymbol;
    EndNode();
}

void TBufferedBinaryYsonWriter::OnBeginList()
{
    EnsureSpace(1);
    BeginCollection(NDetail::BeginListSymbol);
}

void TBufferedBinaryYsonWriter::OnListItem()
{ }

void TBufferedBinaryYsonWriter::OnEndList()
{
    EnsureSpace(2);
    EndCollection(NDetail::EndListSymbol);
    EndNode();
}

void TBufferedBinaryYsonWriter::OnBeginMap()
{
    EnsureSpace(1);
    BeginCollection(NDetail::BeginMapSymbol);
}

void TBufferedBinaryYsonWriter::OnKeyedItem(const TStringBuf& key)
{
    // NB: This call always leaves at least one spare position in buffer.
    WriteStringScalar(key);
    *BufferCursor_++ = NDetail::KeyValueSeparatorSymbol;
}

void TBufferedBinaryYsonWriter::OnEndMap()
{
    EnsureSpace(2);
    EndCollection(NDetail::EndMapSymbol);
    EndNode();
}

void TBufferedBinaryYsonWriter::OnBeginAttributes()
{
    EnsureSpace(1);
    BeginCollection(NDetail::BeginAttributesSymbol);
}

void TBufferedBinaryYsonWriter::OnEndAttributes()
{
    EnsureSpace(1);
    EndCollection(NDetail::EndAttributesSymbol);
}

void TBufferedBinaryYsonWriter::OnRaw(const TStringBuf& yson, EYsonType type)
{
    if (EnableRaw_) {
        size_t length = yson.length();
        if (length <= MaxSmallStringLength) {
            EnsureSpace(length + 1);
            ::memcpy(BufferCursor_, yson.begin(), length);
            BufferCursor_ += length;
        } else {
            Flush();
            Stream_->Write(yson.begin(), length);
        }

        if (type == EYsonType::Node) {
            EndNode();
        }
    } else {
        TYsonConsumerBase::OnRaw(yson, type);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYson
} // namespace NYT
