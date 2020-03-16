#include "config.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

void WarnForUnrecognizedOptions(
    const NLogging::TLogger& logger,
    const NYTree::TYsonSerializablePtr& config)
{
    const auto& Logger = logger;
    auto unrecognized = config->GetUnrecognizedRecursively();
    if (unrecognized && unrecognized->GetChildCount() > 0) {
        YT_LOG_WARNING("Bootstrap config contains unrecognized options (Unrecognized: %v)",
            ConvertToYsonString(unrecognized, NYson::EYsonFormat::Text));
    }
}

void AbortOnUnrecognizedOptions(
    const NLogging::TLogger& logger,
    const NYTree::TYsonSerializablePtr& config)
{
    const auto& Logger = logger;
    auto unrecognized = config->GetUnrecognizedRecursively();
    if (unrecognized && unrecognized->GetChildCount() > 0) {
        YT_LOG_ERROR("Bootstrap config contains unrecognized options, terminating (Unrecognized: %v)",
            ConvertToYsonString(unrecognized, NYson::EYsonFormat::Text));
        YT_ABORT();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

