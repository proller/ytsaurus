#include "stdafx.h"
#include "statistics.h"

#include <core/ytree/fluent.h>
#include <core/ytree/serialize.h>
#include <core/ytree/convert.h>
#include <core/ytree/tree_builder.h>

namespace NYT {
namespace NJobProxy {

////////////////////////////////////////////////////////////////////

TSummary::TSummary()
    : Sum_(0)
    , Count_(0)
    , Min_(std::numeric_limits<i64>::max())
    , Max_(std::numeric_limits<i64>::min())
{ }

TSummary::TSummary(i64 value)
    : Sum_(value)
    , Count_(1)
    , Min_(value)
    , Max_(value)
{ }

void TSummary::Merge(const TSummary& other)
{
    Sum_ += other.Sum_;
    Count_ += other.Count_;
    Min_ = std::min(Min_, other.Min_);
    Max_ = std::max(Max_, other.Max_);
}

void Serialize(const TSummary& summary, NYson::IYsonConsumer* consumer)
{
    NYTree::BuildYsonFluently(consumer)
        .BeginMap()
            .Item("sum").Value(summary.GetSum())
            .Item("count").Value(summary.GetCount())
            .Item("min").Value(summary.GetMin())
            .Item("max").Value(summary.GetMax())
        .EndMap();
}

void Deserialize(TSummary& value, NYTree::INodePtr node)
{
    static std::array<Stroka, 4> possibleKeys = {
        "sum",
        "count",
        "min",
        "max"
    };

    auto mapNode = node->AsMap();
    auto keys = mapNode->GetKeys();
    if (keys.size() != possibleKeys.size()) {
        THROW_ERROR_EXCEPTION("Expected map with %v values but got %v",
            possibleKeys.size(),
            keys.size());
    }

    value.Sum_ = NYTree::ConvertTo<i64>(mapNode->GetChild("sum"));
    value.Count_ = NYTree::ConvertTo<i64>(mapNode->GetChild("count"));
    value.Min_ = NYTree::ConvertTo<i64>(mapNode->GetChild("min"));
    value.Max_ = NYTree::ConvertTo<i64>(mapNode->GetChild("max"));
}

////////////////////////////////////////////////////////////////////

void TStatistics::Add(const NYPath::TYPath& name, const TSummary& summary)
{
    Statistics_[name] = summary;
}

void TStatistics::Merge(const TStatistics& other)
{
    for (const auto& pair : other.Statistics_) {
        Statistics_[pair.first].Merge(pair.second);
    }
}

void TStatistics::Clear()
{
    Statistics_.clear();
}

bool TStatistics::Empty() const
{
    return Statistics_.empty();
}

TSummary TStatistics::GetStatistic(const NYPath::TYPath& name) const
{
    return Statistics_.at(name);
}

void Serialize(const TStatistics& statistics, NYson::IYsonConsumer* consumer)
{
    auto root = NYTree::GetEphemeralNodeFactory()->CreateMap();
    for (const auto& pair : statistics.Statistics_) {
        ForceYPath(root, pair.first);
        auto value = NYTree::ConvertToNode(pair.second);
        SetNodeByYPath(root, pair.first, value);
    }
    NYTree::Serialize(*root, consumer);
}

void Deserialize(TStatistics& value, NYTree::INodePtr node)
{
    try {
        TSummary summary;
        Deserialize(summary, node);
        value.Statistics_.emplace(node->GetPath(), std::move(summary));
    } catch (const std::exception& ) {
        for (auto& pair : node->AsMap()->GetChildren()) {
            Deserialize(value, pair.second);
        }
    }
}

////////////////////////////////////////////////////////////////////

TStatisticsConverter::TStatisticsConverter(TStatisticsConsumer consumer)
    : Depth_(0)
    , TreeBuilder_(NYTree::CreateBuilderFromFactory(NYTree::GetEphemeralNodeFactory()))
    , Consumer_(consumer)
{ }

void TStatisticsConverter::OnStringScalar(const TStringBuf& value)
{
    THROW_ERROR_EXCEPTION("Statistics cannot contain string literals");
}

void TStatisticsConverter::OnInt64Scalar(i64 value)
{
    if (Depth_ == 0) {
        THROW_ERROR_EXCEPTION("Statistics should use map as a container.");
    }
    TreeBuilder_->OnInt64Scalar(value);
}

void TStatisticsConverter::OnBooleanScalar(bool value)
{
    THROW_ERROR_EXCEPTION("Statistics cannot contain booleans. Use integer.");
}

void TStatisticsConverter::OnDoubleScalar(double value)
{
    THROW_ERROR_EXCEPTION("Statistics cannot contain float numbers. Use integer.");
}

void TStatisticsConverter::OnEntity()
{
    THROW_ERROR_EXCEPTION("Statistics cannot contain entity literal.");
}

void TStatisticsConverter::OnBeginList()
{
    THROW_ERROR_EXCEPTION("Statistics cannot contain lists.");
}

void TStatisticsConverter::OnListItem()
{
    TreeBuilder_->BeginTree();
}

void TStatisticsConverter::OnEndList()
{
    YUNREACHABLE();
}

void TStatisticsConverter::OnBeginMap()
{
    ++Depth_;
    TreeBuilder_->OnBeginMap();
}

void TStatisticsConverter::OnKeyedItem(const TStringBuf& key)
{
    TreeBuilder_->OnKeyedItem(key);
}

void TStatisticsConverter::OnEndMap()
{
    TreeBuilder_->OnEndMap();
    --Depth_;
    if (Depth_ == 0) {
        TStatistics statistics;
        ConvertToStatistics(statistics, TreeBuilder_->EndTree());
        Consumer_.Run(statistics);
    }
}

void TStatisticsConverter::OnBeginAttributes()
{
    THROW_ERROR_EXCEPTION("Statistics cannot contain attributes.");
}

void TStatisticsConverter::OnEndAttributes()
{
    YUNREACHABLE();
}

void TStatisticsConverter::ConvertToStatistics(TStatistics& value, NYTree::INodePtr node)
{
    if (node->GetType() == NYTree::ENodeType::Int64) {
        TSummary summary(node->AsInt64()->GetValue());
        value.Add(node->GetPath(), std::move(summary));
        return;
    }

    for (auto& pair : node->AsMap()->GetChildren()) {
        ConvertToStatistics(value, pair.second);
    }
}


////////////////////////////////////////////////////////////////////

} // NJobProxy
} // NYT
