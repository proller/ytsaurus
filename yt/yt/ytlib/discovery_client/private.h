#pragma once

#include <yt/yt/core/logging/log.h>

namespace NYT::NDiscoveryClient {

////////////////////////////////////////////////////////////////////////////////

extern const NLogging::TLogger DiscoveryClientLogger;

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TListMembersRequestSession)
DECLARE_REFCOUNTED_CLASS(TGetGroupMetaRequestSession)
DECLARE_REFCOUNTED_CLASS(THeartbeatSession)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDiscoveryClient
