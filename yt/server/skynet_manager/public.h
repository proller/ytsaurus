#pragma once

#include <yt/core/misc/intrusive_ptr.h>
#include <yt/core/misc/size_literals.h>

namespace NYT {
namespace NSkynetManager {

////////////////////////////////////////////////////////////////////////////////

constexpr size_t SkynetPieceSize = 4_MB;

DECLARE_REFCOUNTED_CLASS(ISkynetApi)

////////////////////////////////////////////////////////////////////////////////

} // namespace NSkynetManager
} // namespace NYT
