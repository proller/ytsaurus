#pragma once

#include "public.h"

#include <yt/client/transaction_client/public.h>

namespace NYT::NHiveClient {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(ETransactionParticipantState,
    (Valid)
    (Invalid)
    (Unregistered)
);

struct ITransactionParticipant
    : public virtual TRefCounted
{
    virtual TCellId GetCellId() const = 0;
    virtual const NTransactionClient::ITimestampProviderPtr& GetTimestampProvider() const = 0;

    virtual ETransactionParticipantState GetState() const = 0;

    virtual TFuture<void> PrepareTransaction(const TTransactionId& transactionId, TTimestamp prepareTimestamp, const TString& user) = 0;
    virtual TFuture<void> CommitTransaction(const TTransactionId& transactionId, TTimestamp commitTimestamp) = 0;
    virtual TFuture<void> AbortTransaction(const TTransactionId& transactionId) = 0;
};

DEFINE_REFCOUNTED_TYPE(ITransactionParticipant)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHiveClient
