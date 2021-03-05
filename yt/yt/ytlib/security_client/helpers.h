#pragma once

#include "public.h"

#include <yt/yt/ytlib/api/native/public.h>

namespace NYT::NSecurityClient {

////////////////////////////////////////////////////////////////////////////////

THashSet<TString> GetSubjectClosure(
    const TString& subject,
    NObjectClient::TObjectServiceProxy& proxy,
    const NApi::NNative::TConnectionConfigPtr& connectionConfig,
    const NApi::TMasterReadOptions& options);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NSecurityClient
