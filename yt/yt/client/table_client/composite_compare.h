#pragma once

#include <yt/yt/core/misc/public.h>

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

int CompareCompositeValues(TStringBuf lhs, TStringBuf rhs);

TFingerprint CompositeHash(TStringBuf compositeValue);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
