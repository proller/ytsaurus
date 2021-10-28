#include "chunk_view.h"

#include "helpers.h"

#include <yt/yt/server/master/cell_master/serialize.h>

namespace NYT::NChunkServer {

using namespace NObjectClient;
using namespace NChunkClient;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

TChunkView::TChunkView(TChunkViewId id)
    : TChunkTree(id)
{ }

void TChunkView::SetUnderlyingTree(TChunkTree* underlyingTree)
{
    YT_VERIFY(underlyingTree);

    switch (underlyingTree->GetType()) {
        case EObjectType::Chunk:
        case EObjectType::ErasureChunk:
            YT_VERIFY(underlyingTree->AsChunk()->GetChunkType() == EChunkType::Table);
            break;

        case EObjectType::SortedDynamicTabletStore:
        case EObjectType::OrderedDynamicTabletStore:
            break;

        default:
            YT_ABORT();
    }

    UnderlyingTree_ = underlyingTree;
}

void TChunkView::SetReadRange(TLegacyReadRange readRange)
{
    YT_VERIFY(!readRange.LowerLimit().HasOffset());
    YT_VERIFY(!readRange.UpperLimit().HasOffset());
    YT_VERIFY(!readRange.LowerLimit().HasChunkIndex());
    YT_VERIFY(!readRange.UpperLimit().HasChunkIndex());
    YT_VERIFY(!readRange.LowerLimit().HasRowIndex());
    YT_VERIFY(!readRange.UpperLimit().HasRowIndex());

    ReadRange_ = std::move(readRange);

    if (readRange.UpperLimit().HasLegacyKey()) {
        const auto& key = readRange.UpperLimit().GetLegacyKey();
        YT_VERIFY(key != NTableClient::MaxKey());
    }
}

TString TChunkView::GetLowercaseObjectName() const
{
    return Format("chunk view %v", GetId());
}

TString TChunkView::GetCapitalizedObjectName() const
{
    return Format("Chunk view %v", GetId());
}

void TChunkView::Save(NCellMaster::TSaveContext& context) const
{
    TChunkTree::Save(context);

    using NYT::Save;

    Save(context, UnderlyingTree_);
    Save(context, ReadRange_);
    Save(context, Parents_);
    Save(context, TransactionId_);
}

void TChunkView::Load(NCellMaster::TLoadContext& context)
{
    TChunkTree::Load(context);

    using NYT::Load;

    // COMPAT(ifsmirnov)
    if (context.GetVersion() < EMasterReign::BackupsInitial) {
        UnderlyingTree_ = Load<TChunk*>(context);
    } else {
        Load(context, UnderlyingTree_);
    }
    Load(context, ReadRange_);
    Load(context, Parents_);
    Load(context, TransactionId_);
}

TLegacyReadLimit TChunkView::GetAdjustedLowerReadLimit(TLegacyReadLimit readLimit) const
{
    if (ReadRange_.LowerLimit().HasLegacyKey()) {
        readLimit.MergeLowerLegacyKey(ReadRange_.LowerLimit().GetLegacyKey());
    }
    return readLimit;
}

TLegacyReadLimit TChunkView::GetAdjustedUpperReadLimit(TLegacyReadLimit readLimit) const
{
    if (ReadRange_.UpperLimit().HasLegacyKey()) {
        readLimit.MergeUpperLegacyKey(ReadRange_.UpperLimit().GetLegacyKey());
    }
    return readLimit;
}

TLegacyReadRange TChunkView::GetCompleteReadRange() const
{
    return {
        GetAdjustedLowerReadLimit(TLegacyReadLimit(GetMinKeyOrThrow(UnderlyingTree_))),
        GetAdjustedUpperReadLimit(TLegacyReadLimit(GetUpperBoundKeyOrThrow(UnderlyingTree_)))
    };
}

void TChunkView::AddParent(TChunkList* parent)
{
    Parents_.push_back(parent);
}

void TChunkView::RemoveParent(TChunkList* parent)
{
    auto it = std::find(Parents_.begin(), Parents_.end(), parent);
    YT_VERIFY(it != Parents_.end());
    Parents_.erase(it);
}

TChunkTreeStatistics TChunkView::GetStatistics() const
{
    return GetChunkTreeStatistics(UnderlyingTree_);
}

int CompareButForReadRange(const TChunkView* lhs, const TChunkView* rhs)
{
    // When ChunkView gets new attributes one should consider them
    // here and merge only views with identical attributes.

    const auto& lhsChunkId = lhs->GetUnderlyingTree()->GetId();
    const auto& rhsChunkId = rhs->GetUnderlyingTree()->GetId();
    const auto& lhsTransactionId = lhs->GetTransactionId();
    const auto& rhsTransactionId = rhs->GetTransactionId();

    if (lhsChunkId != rhsChunkId) {
        return lhsChunkId < rhsChunkId ? -1 : 1;
    } else if  (lhsTransactionId != rhsTransactionId) {
        return lhsTransactionId < rhsTransactionId ? -1 : 1;
    } else {
        return 0;
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkServer
