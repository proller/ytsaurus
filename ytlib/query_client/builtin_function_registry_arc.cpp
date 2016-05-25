#include "function_registry.h"
#include "builtin_functions.h"
#include "user_defined_functions.h"
#include "udf/is_null_arc.h"
#include "udf/sum_arc.h"
#include "udf/farm_hash_arc.h"
#include "udf/int64_arc.h"
#include "udf/uint64_arc.h"

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

IFunctionRegistryPtr CreateBuiltinFunctionRegistry()
{
    auto registry = New<TFunctionRegistry>();

    registry->RegisterFunction(CreateIfFunction());

    registry->RegisterFunction(New<TUserDefinedFunction>(
        "is_null",
        std::vector<TType>{0},
        EValueType::Boolean,
        TSharedRef(
            is_null_bc,
            is_null_bc_len,
            nullptr),
        ECallingConvention::UnversionedValue));

    auto typeArg = TTypeArgument{0};

    auto sumConstraints = std::unordered_map<TTypeArgument, TUnionType>();
    sumConstraints[typeArg] = std::vector<EValueType>{
        EValueType::Int64,
        EValueType::Uint64,
        EValueType::Double};

    registry->RegisterAggregateFunction(New<TUserDefinedAggregateFunction>(
        "sum",
        sumConstraints,
        typeArg,
        typeArg,
        typeArg,
        TSharedRef(
            sum_bc,
            sum_bc_len,
            nullptr),
        ECallingConvention::UnversionedValue));

    auto hashTypes = TUnionType{
        EValueType::Int64,
        EValueType::Uint64,
        EValueType::Boolean,
        EValueType::String};

    registry->RegisterFunction(New<TUserDefinedFunction>(
        "farm_hash",
        std::unordered_map<TTypeArgument, TUnionType>(),
        std::vector<TType>{},
        hashTypes,
        EValueType::Uint64,
        TSharedRef(
            farm_hash_bc,
            farm_hash_bc_len,
            nullptr)));

    auto castConstraints = std::unordered_map<TTypeArgument, TUnionType>();
    castConstraints[typeArg] = std::vector<EValueType>{
        EValueType::Int64,
        EValueType::Uint64,
        EValueType::Double};

    registry->RegisterFunction(New<TUserDefinedFunction>(
        "int64",
        castConstraints,
        std::vector<TType>{typeArg},
        EValueType::Null,
        EValueType::Int64,
        TSharedRef(
            int64_bc,
            int64_bc_len,
            nullptr)));

    registry->RegisterFunction(New<TUserDefinedFunction>(
        "uint64",
        castConstraints,
        std::vector<TType>{typeArg},
        EValueType::Null,
        EValueType::Uint64,
        TSharedRef(
            uint64_bc,
            uint64_bc_len,
            nullptr)));

    return registry;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
