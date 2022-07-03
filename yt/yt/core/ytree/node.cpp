#include "node.h"
#include "convert.h"
#include "node_detail.h"
#include "tree_visitor.h"

#include <yt/yt/core/misc/cast.h>

#include <yt/yt/core/yson/writer.h>

namespace NYT::NYTree {

using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

////////////////////////////////////////////////////////////////////////////////

template <class T>
TString GetSetStringRepresentation(const THashSet<T>& set)
{
    TStringBuilder result;
    result.AppendString("{");
    bool first = true;
    for (const auto& element : set) {
        if (!first) {
            result.AppendString(", ");
        }
        result.AppendFormat("%Qlv", element);
        first = false;
    }
    result.AppendString("}");
    return result.Flush();
}

////////////////////////////////////////////////////////////////////////////////

#define DEFINE_SUPPORTED_TYPES(type, getValueSupportedTypes, setValueSupportedTypes) \
    const THashSet<ENodeType>& TScalarTypeTraits<type>::GetGetValueSupportedTypes() \
    { \
        static const THashSet<ENodeType> Result getValueSupportedTypes; \
        return Result; \
    } \
    \
    const THashSet<ENodeType>& TScalarTypeTraits<type>::GetSetValueSupportedTypes() \
    { \
        static const THashSet<ENodeType> Result setValueSupportedTypes; \
        return Result; \
    } \
    \
    const TString& TScalarTypeTraits<type>::GetGetValueSupportedTypesStringRepresentation() \
    { \
        static const auto Result = GetSetStringRepresentation(TScalarTypeTraits<type>::GetGetValueSupportedTypes()); \
        return Result; \
    } \
    \
    const TString& TScalarTypeTraits<type>::GetSetValueSupportedTypesStringRepresentation() \
    { \
        static const auto Result = GetSetStringRepresentation(TScalarTypeTraits<type>::GetSetValueSupportedTypes()); \
        return Result; \
    }

DEFINE_SUPPORTED_TYPES(
    TString,
    ({
        ENodeType::String,
    }),
    ({
        ENodeType::String,
    }))

const TString& TScalarTypeTraits<TString>::GetValue(const IConstNodePtr& node)
{
    ValidateNodeType(node, GetGetValueSupportedTypes(), GetGetValueSupportedTypesStringRepresentation());
    return node->AsString()->GetValue();
}

void TScalarTypeTraits<TString>::SetValue(const INodePtr& node, const TString& value)
{
    ValidateNodeType(node, GetSetValueSupportedTypes(), GetSetValueSupportedTypesStringRepresentation());
    node->AsString()->SetValue(value);
}

////////////////////////////////////////////////////////////////////////////////

DEFINE_SUPPORTED_TYPES(
    i64,
    ({
        ENodeType::Int64,
        ENodeType::Uint64,
    }),
    ({
        ENodeType::Int64,
        ENodeType::Uint64,
        ENodeType::Double,
    }))

i64 TScalarTypeTraits<i64>::GetValue(const IConstNodePtr& node)
{
    ValidateNodeType(node, GetGetValueSupportedTypes(), GetGetValueSupportedTypesStringRepresentation());
    switch (node->GetType()) {
        case ENodeType::Int64:
            return node->AsInt64()->GetValue();
        case ENodeType::Uint64:
            return CheckedIntegralCast<i64>(node->AsUint64()->GetValue());
        default:
            YT_ABORT();
    }
}

void TScalarTypeTraits<i64>::SetValue(const INodePtr& node, i64 value)
{
    ValidateNodeType(node, GetSetValueSupportedTypes(), GetSetValueSupportedTypesStringRepresentation());
    switch (node->GetType()) {
        case ENodeType::Int64:
            node->AsInt64()->SetValue(value);
            break;
        case ENodeType::Uint64:
            node->AsUint64()->SetValue(CheckedIntegralCast<ui64>(value));
            break;
        case ENodeType::Double:
            node->AsDouble()->SetValue(static_cast<double>(value));
            break;
        default:
            YT_ABORT();
    }
}

////////////////////////////////////////////////////////////////////////////////

DEFINE_SUPPORTED_TYPES(
    ui64,
    ({
        ENodeType::Int64,
        ENodeType::Uint64,
    }),
    ({
        ENodeType::Int64,
        ENodeType::Uint64,
        ENodeType::Double,
    }))

ui64 TScalarTypeTraits<ui64>::GetValue(const IConstNodePtr& node)
{
    ValidateNodeType(node, GetGetValueSupportedTypes(), GetGetValueSupportedTypesStringRepresentation());
    switch (node->GetType()) {
        case ENodeType::Uint64:
            return node->AsUint64()->GetValue();
        case ENodeType::Int64:
            return CheckedIntegralCast<ui64>(node->AsInt64()->GetValue());
        default:
            YT_ABORT();
    }
}

void TScalarTypeTraits<ui64>::SetValue(const INodePtr& node, ui64 value)
{
    ValidateNodeType(node, GetSetValueSupportedTypes(), GetSetValueSupportedTypesStringRepresentation());
    switch (node->GetType()) {
        case ENodeType::Uint64:
            node->AsUint64()->SetValue(value);
            break;
        case ENodeType::Int64:
            node->AsInt64()->SetValue(CheckedIntegralCast<i64>(value));
            break;
        case ENodeType::Double:
            node->AsDouble()->SetValue(static_cast<double>(value));
            break;
        default:
            YT_ABORT();
    }
}

////////////////////////////////////////////////////////////////////////////////

DEFINE_SUPPORTED_TYPES(
    double,
    ({
        ENodeType::Double,
        ENodeType::Int64,
        ENodeType::Uint64,
    }),
    ({
        ENodeType::Double,
    }))

double TScalarTypeTraits<double>::GetValue(const IConstNodePtr& node)
{
    ValidateNodeType(node, GetGetValueSupportedTypes(), GetGetValueSupportedTypesStringRepresentation());
    switch (node->GetType()) {
        case ENodeType::Double:
            return node->AsDouble()->GetValue();
        case ENodeType::Int64:
            return static_cast<double>(node->AsInt64()->GetValue());
        case ENodeType::Uint64:
            return static_cast<double>(node->AsUint64()->GetValue());
        default:
            YT_ABORT();
    }
}

void TScalarTypeTraits<double>::SetValue(const INodePtr& node, double value)
{
    ValidateNodeType(node, GetSetValueSupportedTypes(), GetSetValueSupportedTypesStringRepresentation());
    node->AsDouble()->SetValue(value);
}

////////////////////////////////////////////////////////////////////////////////

DEFINE_SUPPORTED_TYPES(
    bool,
    ({
        ENodeType::Boolean,
    }),
    ({
        ENodeType::Boolean,
    }))

bool TScalarTypeTraits<bool>::GetValue(const IConstNodePtr& node)
{
    ValidateNodeType(node, GetGetValueSupportedTypes(), GetGetValueSupportedTypesStringRepresentation());
    return node->AsBoolean()->GetValue();
}

void TScalarTypeTraits<bool>::SetValue(const INodePtr& node, bool value)
{
    ValidateNodeType(node, GetSetValueSupportedTypes(), GetSetValueSupportedTypesStringRepresentation());
    node->AsBoolean()->SetValue(value);
}

#undef DEFINE_SUPPORTED_TYPES

////////////////////////////////////////////////////////////////////////////////

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

INodePtr IMapNode::GetChildOrThrow(const TString& key) const
{
    auto child = FindChild(key);
    if (!child) {
        ThrowNoSuchChildKey(this, key);
    }
    return child;
}

TString IMapNode::GetChildKeyOrThrow(const IConstNodePtr& child)
{
    auto optionalKey = FindChildKey(child);
    if (!optionalKey) {
        THROW_ERROR_EXCEPTION("Node is not a child");
    }
    return *optionalKey;
}

////////////////////////////////////////////////////////////////////////////////

INodePtr IListNode::GetChildOrThrow(int index) const
{
    auto child = FindChild(index);
    if (!child) {
        ThrowNoSuchChildIndex(this, index);
    }
    return child;
}

int IListNode::GetChildIndexOrThrow(const IConstNodePtr& child)
{
    auto optionalIndex = FindChildIndex(child);
    if (!optionalIndex) {
        THROW_ERROR_EXCEPTION("Node is not a child");
    }
    return *optionalIndex;
}

int IListNode::AdjustChildIndexOrThrow(int index) const
{
    auto adjustedIndex = TryAdjustChildIndex(index, GetChildCount());
    if (!adjustedIndex) {
        ThrowNoSuchChildIndex(this, index);
    }
    return *adjustedIndex;
}

std::optional<int> TryAdjustChildIndex(int index, int childCount)
{
    int adjustedIndex = index >= 0 ? index : index + childCount;
    if (adjustedIndex < 0 || adjustedIndex >= childCount) {
        return std::nullopt;
    }
    return adjustedIndex;
}

////////////////////////////////////////////////////////////////////////////////

void Serialize(INode& value, IYsonConsumer* consumer)
{
    VisitTree(&value, consumer, true /*stable*/, std::nullopt /*attributeKeys*/);
}

void Deserialize(INodePtr& value, const INodePtr& node)
{
    value = node;
}

#define DESERIALIZE_TYPED(type) \
    void Deserialize(I##type##NodePtr& value, const INodePtr& node) \
    { \
        value = node->As##type(); \
    } \
    \
    void Deserialize(I##type##NodePtr& value, NYson::TYsonPullParserCursor* cursor) \
    { \
        auto node = ExtractTo<INodePtr>(cursor); \
        value = node->As##type(); \
    }

DESERIALIZE_TYPED(String)
DESERIALIZE_TYPED(Int64)
DESERIALIZE_TYPED(Uint64)
DESERIALIZE_TYPED(Double)
DESERIALIZE_TYPED(Boolean)
DESERIALIZE_TYPED(Map)
DESERIALIZE_TYPED(List)
DESERIALIZE_TYPED(Entity)

#undef DESERIALIZE_TYPED

////////////////////////////////////////////////////////////////////////////////

void Deserialize(INodePtr& value, NYson::TYsonPullParserCursor* cursor)
{
    auto builder = CreateBuilderFromFactory(GetEphemeralNodeFactory());
    builder->BeginTree();
    cursor->TransferComplexValue(builder.get());
    value = builder->EndTree();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYTree
