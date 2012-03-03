#pragma once

#include "public.h"

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

//! Creates a provider for performing simple, non-cached YPath
//! requests to a given file.
NYTree::TYPathServiceProducer CreateYsonFileProducer(const Stroka& fileName);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
