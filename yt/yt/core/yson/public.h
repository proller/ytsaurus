#pragma once

#include <yt/yt/core/misc/public.h>

#include <library/cpp/yt/yson/public.h>
#include <library/cpp/yt/yson_string/public.h>

namespace NYT::NYson {

////////////////////////////////////////////////////////////////////////////////

enum class ETokenType;

class TYsonProducer;

class TYsonInput;
class TYsonOutput;

class TUncheckedYsonTokenWriter;
class TCheckedYsonTokenWriter;

#ifdef NDEBUG
using TCheckedInDebugYsonTokenWriter = TUncheckedYsonTokenWriter;
#else
using TCheckedInDebugYsonTokenWriter = TCheckedYsonTokenWriter;
#endif

class TTokenizer;

class TProtobufMessageType;

struct IFlushableYsonConsumer;
struct IAsyncYsonConsumer;

enum class EYsonItemType : ui8;
class TYsonItem;
class TYsonPullParser;
class TYsonPullParserCursor;

class TForwardingYsonConsumer;

DEFINE_ENUM(EUnknownYsonFieldsMode,
    (Skip)
    (Fail)
    (Keep)
    (Forward)
);

////////////////////////////////////////////////////////////////////////////////

constexpr int DefaultYsonParserNestingLevelLimit = 64;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYson
