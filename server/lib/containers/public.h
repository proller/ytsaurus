#pragma once

#include <yt/core/misc/public.h>

namespace NYT::NContainers {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EErrorCode,
    ((FailedToStartContainer)  (13000))
);

struct TBind
{
    TString SourcePath;
    TString TargetPath;
    bool IsReadOnly;
};

struct TRootFS
{
    TString RootPath;
    bool IsRootReadOnly;
    std::vector<TBind> Binds;
};

struct TDevice
{
    TString DeviceName;
    bool Enabled;
};

DECLARE_REFCOUNTED_STRUCT(IContainerManager)
DECLARE_REFCOUNTED_STRUCT(IInstance)
DECLARE_REFCOUNTED_STRUCT(IPortoExecutor)

DECLARE_REFCOUNTED_CLASS(TPortoExecutorConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NContainers
