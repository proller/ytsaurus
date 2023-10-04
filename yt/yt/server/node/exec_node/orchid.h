#pragma once

#include "bootstrap.h"

#include <yt/yt/core/ytree/fluent.h>

namespace NYT::NExecNode {

////////////////////////////////////////////////////////////////////////////////

NYTree::IYPathServicePtr GetOrchidService(const IBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NExecNode
