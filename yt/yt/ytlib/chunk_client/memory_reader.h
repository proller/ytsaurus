#pragma once

#include "public.h"

#include <yt/yt/core/misc/ref.h>

namespace NYT::NChunkClient {

////////////////////////////////////////////////////////////////////////////////

IChunkReaderPtr CreateMemoryReader(
    TRefCountedChunkMetaPtr meta,
    std::vector<TBlock> blocks);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkClient
