#pragma once

#include "public.h"
#include "artifact.h"

#include <yt/yt/server/node/cluster_node/public.h>

#include <yt/yt/client/api/public.h>

#include <yt/yt/client/hydra/public.h>

#include <yt/yt/client/ypath/rich.h>

#include <yt/yt/core/logging/public.h>

namespace NYT::NDataNode {

////////////////////////////////////////////////////////////////////////////////

struct TFetchedArtifactKey
{
    NHydra::TRevision ContentRevision;
    std::optional<TArtifactKey> ArtifactKey;
};

TFetchedArtifactKey FetchLayerArtifactKeyIfRevisionChanged(
    const NYPath::TYPath& path,
    NHydra::TRevision contentRevision,
    NClusterNode::TBootstrap const* bootstrap,
    const NLogging::TLogger& logger);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDataNode
