#ifndef PATH_VISITOR_INL_H_
#error "Direct inclusion of this file is not allowed, include path_visitor.h"
// For the sake of sane code completion.
#include "path_visitor.h"
#endif

#include "helpers.h"

namespace NYT::NOrm::NAttributes {

namespace {

////////////////////////////////////////////////////////////////////////////////

// Classification of containers passed to TPathVisitor::Visit. Make sure to drop qualifications on
// the template parameters with std::remove_cvref_t to avoid mismatches.

template <typename TVisitParam>
struct TPathVisitorTraits
{
    static constexpr bool IsVector = false;
    static constexpr bool IsMap = false;
};

template <typename TEntry>
struct TPathVisitorTraits<std::vector<TEntry>>
{
    static constexpr bool IsVector = true;
    static constexpr bool IsMap = false;
};

template <typename TEntry, size_t N>
struct TPathVisitorTraits<TCompactVector<TEntry, N>>
{
    static constexpr bool IsVector = true;
    static constexpr bool IsMap = false;
};

template <typename TKey, typename TValue>
struct TPathVisitorTraits<std::unordered_map<TKey, TValue>>
{
    static constexpr bool IsVector = false;
    static constexpr bool IsMap = true;
};

template <typename TKey, typename TValue>
struct TPathVisitorTraits<std::map<TKey, TValue>>
{
    static constexpr bool IsVector = false;
    static constexpr bool IsMap = true;
};

template <typename TKey, typename TValue>
struct TPathVisitorTraits<THashMap<TKey, TValue>>
{
    static constexpr bool IsVector = false;
    static constexpr bool IsMap = true;
};

} // namespace

////////////////////////////////////////////////////////////////////////////////

template <typename TValue>
TValue TPathVisitorUtil::ValueOrThrow(TErrorOr<TValue> errorOrValue) const
{
    if (errorOrValue.IsOK()) {
        return std::move(errorOrValue).Value();
    } else {
        Throw(std::move(errorOrValue));
    }
}

template <typename... Args>
[[noreturn]] void TPathVisitorUtil::Throw(Args&&... args) const
{
    THROW_ERROR TError(std::forward<Args>(args)...)
        << TErrorAttribute("path", Tokenizer_.GetPath())
        << TErrorAttribute("position", CurrentPath_.GetPath());
}

////////////////////////////////////////////////////////////////////////////////

// Every method call begins with "self" which has the type of the concrete class;
// everything is iterable... feels like Python.

template <typename TSelf>
template <typename TVisitParam>
void TPathVisitor<TSelf>::Visit(TVisitParam&& target, NYPath::TYPathBuf path)
{
    Self()->Reset(path);
    Self()->VisitGeneric(std::forward<TVisitParam>(target), EVisitReason::TopLevel);
}

template <typename TSelf>
TSelf* TPathVisitor<TSelf>::Self()
{
    return static_cast<TSelf*>(this);
}

template <typename TSelf>
const TSelf* TPathVisitor<TSelf>::Self() const
{
    return static_cast<const TSelf*>(this);
}

template <typename TSelf>
template <typename TVisitParam>
void TPathVisitor<TSelf>::VisitGeneric(
    TVisitParam&& target,
    EVisitReason reason)
{
    using TTraits = TPathVisitorTraits<std::remove_cvref_t<TVisitParam>>;

    if constexpr (TTraits::IsVector) {
        Self()->VisitVector(std::forward<TVisitParam>(target), reason);
    } else if constexpr (TTraits::IsMap) {
        Self()->VisitMap(std::forward<TVisitParam>(target), reason);
    } else {
        Self()->VisitOther(std::forward<TVisitParam>(target), reason);
    }
}

template <typename TSelf>
template <typename TVisitParam>
void TPathVisitor<TSelf>::VisitVector(
    TVisitParam&& target,
    EVisitReason reason)
{
    if (Self()->PathComplete()) {
        if (Self()->GetVisitEverythingAfterPath()) {
            Self()->VisitWholeVector(
                std::forward<TVisitParam>(target),
                EVisitReason::AfterPath);
            return;
        } else {
            Self()->Throw(EErrorCode::Unimplemented, "Cannot handle whole vectors");
        }
    }

    Self()->SkipSlash();

    if (Self()->GetTokenizerType() == NYPath::ETokenType::Asterisk) {
        Self()->AdvanceOverAsterisk();
        Self()->VisitWholeVector(
            std::forward<TVisitParam>(target),
            EVisitReason::Asterisk);
    } else {
        int size = target.size();
        auto errorOrIndexParseResult = Self()->ParseCurrentListIndex(size);
        if (!errorOrIndexParseResult.IsOK()) {
            Self()->OnVectorIndexError(
                std::forward<TVisitParam>(target),
                reason,
                std::move(errorOrIndexParseResult));
            return;
        }

        auto& indexParseResult = errorOrIndexParseResult.Value();
        Self()->AdvanceOver(ui64(indexParseResult.Index));

        switch (indexParseResult.IndexType) {
            case EListIndexType::Absolute:
                Self()->VisitGeneric(target[indexParseResult.Index], EVisitReason::Path);
                break;
            case EListIndexType::Relative:
                Self()->VisitVectorEntryRelative(
                    std::forward<TVisitParam>(target),
                    indexParseResult.Index,
                    EVisitReason::Path);
                break;
            default:
                YT_ABORT();
        }
    }
}

template <typename TSelf>
template <typename TVisitParam>
void TPathVisitor<TSelf>::VisitWholeVector(
    TVisitParam&& target,
    EVisitReason reason)
{
    for (size_t index = 0; !Self()->StopIteration_ && index < target.size(); ++index) {
        auto checkpoint = Self()->CheckpointBranchedTraversal(index);
        Self()->VisitGeneric(target[index], reason);
    }
}

template <typename TSelf>
template <typename TVisitParam>
void TPathVisitor<TSelf>::VisitVectorEntryRelative(
    TVisitParam&& target,
    int index,
    EVisitReason reason)
{
    Y_UNUSED(target);
    Y_UNUSED(reason);

    Self()->Throw(EErrorCode::MalformedPath,
        "Unexpected relative path specifier %v (producing an index of %v)",
        Self()->GetToken(),
        index);
}

template <typename TSelf>
template <typename TVisitParam>
void TPathVisitor<TSelf>::OnVectorIndexError(
    TVisitParam&& target,
    EVisitReason reason,
    TError error)
{
    Y_UNUSED(target);
    Y_UNUSED(reason);

    if (Self()->AllowMissing_ && error.GetCode() == EErrorCode::OutOfBounds) {
        return;
    }

    Self()->Throw(std::move(error));
}

template <typename TSelf>
template <typename TVisitParam>
void TPathVisitor<TSelf>::VisitMap(
    TVisitParam&& target,
    EVisitReason reason)
{
    if (Self()->PathComplete()) {
        if (Self()->VisitEverythingAfterPath_) {
            Self()->VisitWholeMap(
                std::forward<TVisitParam>(target),
                EVisitReason::AfterPath);
            return;
        } else {
            Self()->Throw(EErrorCode::Unimplemented, "Cannot handle whole message maps");
        }
    }

    Self()->SkipSlash();

    if (Self()->GetTokenizerType() == NYPath::ETokenType::Asterisk) {
        Self()->AdvanceOverAsterisk();
        Self()->VisitWholeMap(
            std::forward<TVisitParam>(target),
            EVisitReason::Asterisk);
    } else {
        Self()->Expect(NYPath::ETokenType::Literal);

        TString key = Self()->GetLiteralValue();
        Self()->AdvanceOver(key);

        typename std::remove_reference_t<TVisitParam>::key_type mapKey;
        if constexpr (std::is_same_v<TString, decltype(mapKey)>) {
            mapKey = key;
        } else {
            if (!TryFromString(key, mapKey)) {
                Self()->Throw(EErrorCode::MalformedPath, "Invalid map key %v", key);
            }
        }

        auto it = target.find(mapKey);
        if (it == target.end()) {
            Self()->OnMapKeyError(
                std::forward<TVisitParam>(target),
                std::move(mapKey),
                std::move(key),
                reason);
            return;
        }

        Self()->VisitGeneric(it->second, EVisitReason::Path);
    }
}

template <typename TSelf>
template <typename TVisitParam>
void TPathVisitor<TSelf>::VisitWholeMap(
    TVisitParam&& target,
    EVisitReason reason)
{
    for (auto& [key, entry] : target) {
        auto checkpoint = Self()->CheckpointBranchedTraversal(key);
        Self()->VisitGeneric(entry, reason);
    }
}

template <typename TSelf>
template <typename TVisitParam, typename TMapKey>
void TPathVisitor<TSelf>::OnMapKeyError(
    TVisitParam&& target,
    TMapKey mapKey,
    TString key,
    EVisitReason reason)
{
    Y_UNUSED(target);
    Y_UNUSED(mapKey);
    Y_UNUSED(reason);

    if (Self()->AllowMissing_) {
        return;
    }

    Self()->Throw(EErrorCode::MissingKey, "Key %v not found in map", key);
}

template <typename TSelf>
template <typename TVisitParam>
void TPathVisitor<TSelf>::VisitOther(
    TVisitParam&& target,
    EVisitReason reason)
{
    Y_UNUSED(target);
    Y_UNUSED(reason);

    Self()->Throw(EErrorCode::Unimplemented, "Cannot visit type %v", TypeName<TVisitParam>());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NOrm::NAttributes
