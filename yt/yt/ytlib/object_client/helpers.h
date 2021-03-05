#pragma once

#include "public.h"

#include <yt/yt/client/object_client/public.h>

#include <yt/yt/core/rpc/public.h>

namespace NYT::NObjectClient {

////////////////////////////////////////////////////////////////////////////////

void AddCellTagToSyncWith(const NRpc::IClientRequestPtr& request, NObjectClient::TCellTag cellTag);
void AddCellTagToSyncWith(const NRpc::IClientRequestPtr& request, NObjectClient::TObjectId objectId);

bool IsRetriableObjectServiceError(int attempt, const TError& error);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NObjectClient
