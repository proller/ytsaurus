#pragma once

#include <core/misc/public.h>

namespace ev {

////////////////////////////////////////////////////////////////////////////////

struct dynamic_loop;

////////////////////////////////////////////////////////////////////////////////

} // namespace ev

namespace NYT {
namespace NPipes {

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TAsyncReader);
DECLARE_REFCOUNTED_CLASS(TAsyncWriter);

namespace NDetail {

DECLARE_REFCOUNTED_CLASS(TAsyncReaderImpl)
DECLARE_REFCOUNTED_CLASS(TAsyncWriterImpl)

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

} // namespace NPipes
} // namespace NYT
