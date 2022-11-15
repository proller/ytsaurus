#include "serialize.h"

#include <util/generic/cast.h>

namespace NYT::NClusterClock {

using namespace NHydra;

////////////////////////////////////////////////////////////////////////////////

namespace {

template <class E>
typename std::enable_if<TEnumTraits<E>::IsEnum, bool>::type operator == (E lhs, typename TEnumTraits<E>::TUnderlying rhs)
{
    return static_cast<typename TEnumTraits<E>::TUnderlying>(lhs) == rhs;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

TReign GetCurrentReign()
{
    return ToUnderlying(TEnumTraits<EClockReign>::GetMaxValue());
}

bool ValidateSnapshotReign(TReign reign)
{
    for (auto v : TEnumTraits<EClockReign>::GetDomainValues()) {
        if (v == reign) {
            return true;
        }
    }
    return false;
}

EFinalRecoveryAction GetActionToRecoverFromReign(TReign reign)
{
    // In Clock we do it the hard way.
    YT_VERIFY(reign == GetCurrentReign());

    return EFinalRecoveryAction::None;
}

////////////////////////////////////////////////////////////////////////////////

TSaveContext::TSaveContext(ICheckpointableOutputStream* output)
    : NHydra::TSaveContext(output, GetCurrentReign())
{ }

EClockReign TSaveContext::GetVersion()
{
    return static_cast<EClockReign>(NHydra::TSaveContext::GetVersion());
}

////////////////////////////////////////////////////////////////////////////////

TLoadContext::TLoadContext(TBootstrap* bootstrap)
    : Bootstrap_(bootstrap)
{ }

EClockReign TLoadContext::GetVersion()
{
    return static_cast<NClusterClock::EClockReign>(NHydra::TLoadContext::GetVersion());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClusterClock
