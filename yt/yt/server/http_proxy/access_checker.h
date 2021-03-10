#pragma once

#include "public.h"

namespace NYT::NHttpProxy {

////////////////////////////////////////////////////////////////////////////////

struct IAccessChecker
    : public TRefCounted
{
    virtual TError ValidateAccess(const TString& user) const = 0;
};

DEFINE_REFCOUNTED_TYPE(IAccessChecker)

////////////////////////////////////////////////////////////////////////////////

IAccessCheckerPtr CreateAccessChecker(TBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHttpProxy
