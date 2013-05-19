#ifndef CONVERT_INL_H_
#error "Direct inclusion of this file is not allowed, include convert.h"
#endif
#undef CONVERT_INL_H_

#include "serialize.h"
#include "tree_builder.h"
#include "yson_stream.h"
#include "yson_producer.h"
#include "attribute_helpers.h"

#include <ytlib/ypath/token.h>
#include <ytlib/yson/tokenizer.h>
#include <ytlib/yson/parser.h>

#include <util/generic/typehelpers.h>
#include <util/generic/static_assert.h>

#include <ytlib/misc/preprocessor.h>
#include <ytlib/misc/small_vector.h>

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

template <class T>
void Consume(const T& value, NYson::IYsonConsumer* consumer)
{
    // Check that T differs from Stroka to prevent
    // accident usage of Stroka instead of TYsonString.
    static_assert(!TSameType<T, Stroka>::Result,
        "Are you sure that you want to convert from Stroka, not from TYsonString? "
        "In this case use TRawString wrapper on Stroka.");

    Serialize(value, consumer);
}

////////////////////////////////////////////////////////////////////////////////

template <class T>
TYsonProducer ConvertToProducer(T&& value)
{
    auto type = GetYsonType(value);
    auto callback = BIND(
        [] (const T& value, NYson::IYsonConsumer* consumer) {
            Consume(value, consumer);
        },
        std::forward<T>(value));
    return TYsonProducer(callback, type);
}

template <class T>
TYsonString ConvertToYsonString(const T& value)
{
    return ConvertToYsonString(value, NYson::EYsonFormat::Binary);
}

template <class T>
TYsonString ConvertToYsonString(const T& value, NYson::EYsonFormat format)
{
    auto type = GetYsonType(value);
    Stroka result;
    TStringOutput stringOutput(result);
    WriteYson(&stringOutput, value, type, format);
    return TYsonString(result, type);
}

////////////////////////////////////////////////////////////////////////////////

template <class T>
INodePtr ConvertToNode(
    const T& value,
    INodeFactoryPtr factory)
{
    auto type = GetYsonType(value);

    auto builder = CreateBuilderFromFactory(factory);
    builder->BeginTree();

    switch (type) {
        case NYson::EYsonType::ListFragment:
            builder->OnBeginList();
            break;
        case NYson::EYsonType::MapFragment:
            builder->OnBeginMap();
            break;
        default:
            break;
    }

    Consume(value, ~builder);

    switch (type) {
        case NYson::EYsonType::ListFragment:
            builder->OnEndList();
            break;
        case NYson::EYsonType::MapFragment:
            builder->OnEndMap();
            break;
        default:
            break;
    }

    return builder->EndTree();
}

////////////////////////////////////////////////////////////////////////////////

template <class T>
std::unique_ptr<IAttributeDictionary> ConvertToAttributes(const T& value)
{
    auto attributes = CreateEphemeralAttributes();
    TAttributeConsumer consumer(~attributes);
    Consume(value, &consumer);
    return attributes;
}

////////////////////////////////////////////////////////////////////////////////

template <class TTo>
TTo ConvertTo(INodePtr node)
{
    TTo result;
    Deserialize(result, node);
    return result;
}

template <class TTo, class TFrom>
TTo ConvertTo(const TFrom& value)
{
    return ConvertTo<TTo>(ConvertToNode(value));
}

// Cannot inline this into CHECK_TYPE because
// C++ has some issues while expanding type argument
#define GET_VALUE(x) token.Get ## x ## Value()

#define CHECK_TYPE(type) \
    if (token.GetType() == ETokenType::type) { \
        return GET_VALUE(type); \
    } \
    consideredTokens.push_back(ETokenType::type);

#define CONVERT_TO_PLAIN_TYPE(type, token_types) \
    template <> \
    inline type ConvertTo(const TYsonString& str) \
    { \
        using NYson::ETokenType; \
        NYson::TTokenizer tokenizer(str.Data()); \
        if (tokenizer.ParseNext()) { \
            auto token = tokenizer.CurrentToken(); \
            TSmallVector<ETokenType, 2> consideredTokens; \
            PP_FOR_EACH(CHECK_TYPE, token_types); \
            token.CheckType( \
                std::vector<ETokenType>( \
                    consideredTokens.begin(), \
                    consideredTokens.end())); \
        } \
        THROW_ERROR_EXCEPTION("Cannot parse " PP_STRINGIZE(token_type) " from string %s", ~str.Data().Quote()); \
    }

CONVERT_TO_PLAIN_TYPE(i64, (Integer))
CONVERT_TO_PLAIN_TYPE(double, (Double)(Integer))
CONVERT_TO_PLAIN_TYPE(TStringBuf, (String))

#undef CONVERT_TO_PLAIN_TYPE
#undef CHECK_TYPE
#undef GET_VALUE

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
