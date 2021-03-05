#pragma once

#include <yt/yt/core/ytree/yson_serializable.h>

namespace NYT::NJson {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EJsonFormat,
    (Text)
    (Pretty)
);

DEFINE_ENUM(EJsonAttributesMode,
    (Always)
    (Never)
    (OnDemand)
);

class TJsonFormatConfig
    : public NYTree::TYsonSerializable
{
public:
    EJsonFormat Format;
    EJsonAttributesMode AttributesMode;
    bool Plain;
    bool EncodeUtf8;
    i64 MemoryLimit;

    std::optional<int> StringLengthLimit;

    bool Stringify;
    bool AnnotateWithTypes;

    bool SupportInfinity;
    bool StringifyNanAndInfinity;

    // Size of buffer used read out input stream in parser.
    // NB: in case of parsing long string yajl holds in memory whole string prefix and copy it on every parse call.
    // Therefore parsing long strings works faster with larger buffer.
    int BufferSize;

    //! Only works for tabular data.
    bool SkipNullValues;

    TJsonFormatConfig()
    {
        RegisterParameter("format", Format)
            .Default(EJsonFormat::Text);
        RegisterParameter("attributes_mode", AttributesMode)
            .Default(EJsonAttributesMode::OnDemand);
        RegisterParameter("plain", Plain)
            .Default(false);
        RegisterParameter("encode_utf8", EncodeUtf8)
            .Default(true);
        RegisterParameter("string_length_limit", StringLengthLimit)
            .Default();
        RegisterParameter("stringify", Stringify)
            .Default(false);
        RegisterParameter("annotate_with_types", AnnotateWithTypes)
            .Default(false);
        RegisterParameter("support_infinity", SupportInfinity)
            .Default(false);
        RegisterParameter("stringify_nan_and_infinity", StringifyNanAndInfinity)
            .Default(false);
        RegisterParameter("buffer_size", BufferSize)
            .Default(16 * 1024);
        RegisterParameter("skip_null_values", SkipNullValues)
            .Default(false);

        MemoryLimit = 256_MB;

        RegisterPostprocessor([&] () {
            if (SupportInfinity && StringifyNanAndInfinity) {
                THROW_ERROR_EXCEPTION("\"support_infinity\" and \"stringify_nan_and_infinity\" "
                    "cannot be specified simultaneously");
            }
        });
    }
};

DEFINE_REFCOUNTED_TYPE(TJsonFormatConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJson
