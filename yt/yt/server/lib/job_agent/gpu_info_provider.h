#pragma once

#include "public.h"
#include "private.h"

#include "config.h"

namespace NYT::NJobAgent {

////////////////////////////////////////////////////////////////////////////////

struct TGpuInfo
{
    TInstant UpdateTime;
    int Index = -1;
    double UtilizationGpuRate = 0.0;
    double UtilizationMemoryRate = 0.0;
    i64 MemoryUsed = 0;
    i64 MemoryTotal = 0;
    double PowerDraw = 0.0;
    double PowerLimit = 0.0;
    i64 ClocksSm = 0;
    i64 ClocksMaxSm = 0;
    double SMUtilizationRate = 0.0;
    double SMOccupancyRate = 0.0;
    TString Name;
};

struct IGpuInfoProvider
    : public TRefCounted
{
    virtual std::vector<TGpuInfo> GetGpuInfos(TDuration checkTimeout) = 0;
};

DEFINE_REFCOUNTED_TYPE(IGpuInfoProvider);

IGpuInfoProviderPtr CreateGpuInfoProvider(const TGpuInfoSourceConfigPtr& gpuInfoSource);

////////////////////////////////////////////////////////////////////////////////

} // namespace NY::NJobAgent
