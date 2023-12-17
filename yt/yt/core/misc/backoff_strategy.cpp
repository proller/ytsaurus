#include "backoff_strategy.h"
#include "jitter.h"

#include <util/random/normal.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

TConstantBackoffOptions::operator TExponentialBackoffOptions() const
{
    return TExponentialBackoffOptions{
        .InvocationCount = InvocationCount,
        .MinBackoff = Backoff,
        .MaxBackoff = Backoff,
        .BackoffMultiplier = 1.0,
        .BackoffJitter = BackoffJitter
    };
}

////////////////////////////////////////////////////////////////////////////////

TBackoffStrategy::TBackoffStrategy(const TExponentialBackoffOptions& options)
    : Options_(options)
{
    Restart();
}

void TBackoffStrategy::Restart()
{
    InvocationIndex_ = 0;
    Backoff_ = Options_.MinBackoff;
    ApplyJitter();
}

bool TBackoffStrategy::Next()
{
    if (InvocationIndex_ > 0) {
        Backoff_ = std::min(Backoff_ * Options_.BackoffMultiplier, Options_.MaxBackoff);
        ApplyJitter();
    }
    return ++InvocationIndex_ < Options_.InvocationCount;
}

int TBackoffStrategy::GetInvocationIndex() const
{
    return InvocationIndex_;
}

int TBackoffStrategy::GetInvocationCount() const
{
    return Options_.InvocationCount;
}

TDuration TBackoffStrategy::GetBackoff() const
{
    return BackoffWithJitter_;
}

void TBackoffStrategy::ApplyJitter()
{
    BackoffWithJitter_ = ::NYT::ApplyJitter(Backoff_, Options_.BackoffJitter, [] {
        // StdNormalRandom is unlikely to produce a value outside of [-Max, Max] range.
        constexpr double Max = 7.0;
        return std::clamp(StdNormalRandom<double>() / Max, -1.0, +1.0);
    });
}

void TBackoffStrategy::UpdateOptions(const TExponentialBackoffOptions& newOptions)
{
    Options_ = newOptions;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
