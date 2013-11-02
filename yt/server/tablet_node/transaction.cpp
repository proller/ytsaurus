#include "stdafx.h"
#include "transaction.h"
#include "automaton.h"

#include <server/hydra/composite_automaton.h>

namespace NYT {
namespace NTabletNode {

using namespace NTransactionClient;

////////////////////////////////////////////////////////////////////////////////

TTransaction::TTransaction(const TTransactionId& id)
    : Id_(id)
    , StartTime_(TInstant::Zero())
    , State_(ETransactionState::Active)
    , StartTimestamp_(NullTimestamp)
    , PrepareTimestamp_(NullTimestamp)
{ }

void TTransaction::Save(TSaveContext& context) const
{
    using NYT::Save;
    Save(context, Id_);
    Save(context, Timeout_);
    Save(context, StartTime_);
    Save(context, State_ == ETransactionState::TransientlyPrepared ? ETransactionState(ETransactionState::Active) : State_);
    Save(context, StartTimestamp_);
    Save(context, State_ == ETransactionState::TransientlyPrepared ? NullTimestamp : PrepareTimestamp_);
}

void TTransaction::Load(TLoadContext& context)
{
    using NYT::Load;
    Load(context, Id_);
    Load(context, Timeout_);
    Load(context, StartTime_);
    Load(context, State_);
    Load(context, StartTimestamp_);
    Load(context, PrepareTimestamp_);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT

