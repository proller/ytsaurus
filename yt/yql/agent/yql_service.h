#pragma once

#include "private.h"

#include <yt/yt/core/rpc/public.h>

namespace NYT::NYqlAgent {

////////////////////////////////////////////////////////////////////////////////

NRpc::IServicePtr CreateYqlService(IInvokerPtr controlInvoker, IYqlAgentPtr yqlAgent);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYqlAgent
