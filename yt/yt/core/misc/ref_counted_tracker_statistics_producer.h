#pragma once

#include "ref_counted_tracker.h"

#include <yt/yt/core/yson/public.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

NYson::TYsonProducer CreateRefCountedTrackerStatisticsProducer();

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
