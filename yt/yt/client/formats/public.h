#pragma once

#include <yt/yt/core/misc/public.h>

namespace NYT::NFormats {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EComplexTypeMode,
    (Positional)
    (Named)
);

//! Type of data that can be read or written by a driver command.
DEFINE_ENUM(EDataType,
    (Null)
    (Binary)
    (Structured)
    (Tabular)
);

DEFINE_ENUM(EFormatType,
    (Null)
    (Yson)
    (Json)
    (Dsv)
    (Yamr)
    (YamredDsv)
    (SchemafulDsv)
    (Protobuf)
    (WebJson)
    (Skiff)
);

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TYsonFormatConfig)
DECLARE_REFCOUNTED_CLASS(TTableFormatConfigBase)
DECLARE_REFCOUNTED_CLASS(TYamrFormatConfig)
DECLARE_REFCOUNTED_CLASS(TYamrFormatConfigBase)
DECLARE_REFCOUNTED_CLASS(TDsvFormatConfig)
DECLARE_REFCOUNTED_CLASS(TDsvFormatConfigBase)
DECLARE_REFCOUNTED_CLASS(TYamredDsvFormatConfig)
DECLARE_REFCOUNTED_CLASS(TSchemafulDsvFormatConfig)
DECLARE_REFCOUNTED_CLASS(TProtobufColumnConfig)
DECLARE_REFCOUNTED_CLASS(TProtobufTableConfig)
DECLARE_REFCOUNTED_CLASS(TProtobufFormatConfig)
DECLARE_REFCOUNTED_CLASS(TWebJsonFormatConfig)
DECLARE_REFCOUNTED_CLASS(TSkiffFormatConfig)

DECLARE_REFCOUNTED_STRUCT(IYamrConsumer)

DECLARE_REFCOUNTED_STRUCT(ISchemalessFormatWriter)

DECLARE_REFCOUNTED_CLASS(TControlAttributesConfig)

struct IParser;

class TEscapeTable;

class TFormat;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NFormats
