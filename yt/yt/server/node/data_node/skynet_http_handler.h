#pragma once

#include <yt/yt/core/http/public.h>

#include <yt/yt/server/node/cluster_node/bootstrap.h>

namespace NYT::NDataNode {

////////////////////////////////////////////////////////////////////////////////

NHttp::IHttpHandlerPtr MakeSkynetHttpHandler(NClusterNode::TBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDataNode
