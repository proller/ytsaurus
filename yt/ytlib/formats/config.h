#pragma once

#include "public.h"

#include <ytlib/misc/configurable.h>

namespace NYT {
namespace NFormats {

////////////////////////////////////////////////////////////////////////////////

struct TTsvFormatConfig
    : public TConfigurable
{
    char RecordSeparator;
    char KeyValueSeparator;
    char FieldSeparator;
    TNullable<Stroka> LinePrefix;

    TTsvFormatConfig()
    {
        Register("record_separator", RecordSeparator).Default('\n');
        Register("key_value_separator", KeyValueSeparator).Default('=');
        Register("field_separator", FieldSeparator).Default('\t');
        Register("line_prefix", LinePrefix).Default();
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NFormats
} // namespace NYT
