#include "stdafx.h"
#include "composite_meta_state.h"
#include "composite_meta_state_detail.h"

#include "../misc/foreach.h"
#include "../misc/assert.h"

namespace NYT {
namespace NMetaState {

using namespace NProto;

////////////////////////////////////////////////////////////////////////////////

TMetaStatePart::TMetaStatePart(
    TMetaStateManager::TPtr metaStateManager,
    TCompositeMetaState::TPtr metaState)
    : MetaStateManager(metaStateManager)
    , MetaState(metaState)
{
    YASSERT(~metaStateManager != NULL);
    YASSERT(~metaState != NULL);

    metaStateManager->OnStartLeading().Subscribe(FromMethod(
        &TThis::OnStartLeading,
        TPtr(this)));
    metaStateManager->OnLeaderRecoveryComplete().Subscribe(FromMethod(
        &TThis::OnLeaderRecoveryComplete,
        TPtr(this)));
    metaStateManager->OnStopLeading().Subscribe(FromMethod(
        &TThis::OnStopLeading,
        TPtr(this)));
}

void TMetaStatePart::Load(TInputStream* input)
{
    UNUSED(input);
}

TFuture<TVoid>::TPtr TMetaStatePart::Save(TOutputStream* output, IInvoker::TPtr invoker)
{
    UNUSED(output);
    // NB: Same as in Load.
    return 
        FromFunctor([] () { return TVoid(); })
        ->AsyncVia(invoker)
        ->Do();
}

void TMetaStatePart::Clear()
{ }

bool TMetaStatePart::IsLeader() const
{
    auto status = MetaStateManager->GetStateStatus();
    return status == EPeerStatus::Leading;
}

bool TMetaStatePart::IsFolllower() const
{
    auto status = MetaStateManager->GetStateStatus();
    return status == EPeerStatus::Following;
}

bool TMetaStatePart::IsRecovery() const
{
    auto status = MetaStateManager->GetStateStatus();
    return status == EPeerStatus::LeaderRecovery || status == EPeerStatus::FollowerRecovery;
}

void TMetaStatePart::OnStartLeading()
{ }

void TMetaStatePart::OnLeaderRecoveryComplete()
{ }

void TMetaStatePart::OnStopLeading()
{ }

////////////////////////////////////////////////////////////////////////////////

void TCompositeMetaState::RegisterPart(TMetaStatePart::TPtr part)
{
    YASSERT(~part != NULL);

    Stroka partName = part->GetPartName();
    YVERIFY(Parts.insert(MakePair(partName, part)).Second());
}

TFuture<TVoid>::TPtr TCompositeMetaState::Save(TOutputStream* output, IInvoker::TPtr invoker)
{
    TFuture<TVoid>::TPtr result;
    FOREACH(auto& pair, Parts) {
        result = pair.Second()->Save(output, invoker);
    }
    return result;
}

void TCompositeMetaState::Load(TInputStream* input)
{
    FOREACH(auto& pair, Parts) {
        pair.Second()->Load(input);
    }
}

void TCompositeMetaState::ApplyChange(const TRef& changeData)
{
    TMsgChangeHeader header;
    TRef messageData;
    DeserializeChange(
        changeData,
        &header,
        &messageData);

    Stroka changeType = header.GetChangeType();

    auto it = Methods.find(changeType);
    YASSERT(it != Methods.end());

    it->Second()->Do(messageData);
}

void TCompositeMetaState::Clear()
{
    FOREACH(auto& pair, Parts) {
        pair.Second()->Clear();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
