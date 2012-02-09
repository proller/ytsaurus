#include "stdafx.h"
#include "redirector_service_base.h"
#include "channel_cache.h"

namespace NYT {
namespace NRpc {

using namespace NBus;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = RpcLogger;
static TChannelCache ChannelCache;

////////////////////////////////////////////////////////////////////////////////

class TRedirectorServiceBase::TRequest
    : public IClientRequest
{
public:
    TRequest(
        IMessage::TPtr message,
        const TRequestId& requestId,
        const Stroka& path,
        const Stroka& verb)
        : Message(message)
        , RequestId(requestId)
        , Path(path)
        , Verb(verb)
    { }

    IMessage::TPtr Serialize() const
    {
        return Message;
    }

    const TRequestId& GetRequestId() const
    {
        return RequestId;
    }

    const Stroka& GetPath() const
    {
        return Path;
    }
    const Stroka& GetVerb() const
    {
        return Verb;
    }

private:
    TRequestId RequestId;
    Stroka Path;
    Stroka Verb;
    IMessage::TPtr Message;
};

////////////////////////////////////////////////////////////////////////////////

class TRedirectorServiceBase::TResponseHandler
    : public IClientResponseHandler
{
public:
    TResponseHandler(IServiceContext* context)
        : Context(context)
    { }

    void OnAcknowledgement()
    { }

    void OnResponse(NBus::IMessage* message)
    {
        Context->Reply(message);
    }

    void OnError(const TError& error)
    {
        Context->Reply(error);
    }

private:
    IServiceContext::TPtr Context;

};

////////////////////////////////////////////////////////////////////////////////

TRedirectorServiceBase::TRedirectorServiceBase(
    const Stroka& serviceName,
    const Stroka& loggingCategory)
    : ServiceName(serviceName)
    , LoggingCategory(loggingCategory)
{ }

void TRedirectorServiceBase::OnBeginRequest(IServiceContext* context)
{
    // TODO(babenko): use AsStrong
    auto context_= IServiceContext::TPtr(context);
    HandleRedirect(context)->Subscribe(FromFunctor([=] (TRedirectResult result)
        {
            if (!result.IsOK()) {
                context_->Reply(TError(
                    NRpc::EErrorCode::Unavailable,
                    Sprintf("Redirection failed\n%s", ~result.GetMessage())));
                return;
            }

            const auto& params = result.Value();

            context_->SetRequestInfo(Sprintf("Address: %s, Timeout: %d",
                ~params.Address,
                static_cast<int>(params.Timeout.MilliSeconds())));

            auto channel = ChannelCache.GetChannel(params.Address);

            auto request = New<TRequest>(
                context_->GetRequestMessage(),
                context_->GetRequestId(),
                context_->GetPath(),
                context_->GetVerb());

            auto responseHandler = New<TResponseHandler>(~context_);
            channel->Send(~request, ~responseHandler, params.Timeout);
        }));
}

void TRedirectorServiceBase::OnEndRequest(IServiceContext* context)
{
    UNUSED(context);
}

Stroka TRedirectorServiceBase::GetServiceName() const
{
    return ServiceName;
}

Stroka TRedirectorServiceBase::GetLoggingCategory() const
{
    return LoggingCategory;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc
} // namespace NYT
