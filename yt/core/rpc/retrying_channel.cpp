#include "retrying_channel.h"
#include "private.h"
#include "channel_detail.h"
#include "client.h"
#include "config.h"

#include <yt/core/bus/client.h>

#include <util/system/guard.h>
#include <util/system/spinlock.h>

namespace NYT::NRpc {

using namespace NBus;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = RpcClientLogger;

////////////////////////////////////////////////////////////////////////////////

class TRetryingChannel
    : public TChannelWrapper
{
public:
    TRetryingChannel(
        TRetryingChannelConfigPtr config,
        IChannelPtr underlyingChannel,
        TCallback<bool(const TError&)> isRetriableError)
        : TChannelWrapper(std::move(underlyingChannel))
        , Config_(std::move(config))
        , IsRetriableError_(isRetriableError)
    {
        YT_VERIFY(Config_);
    }

    virtual IClientRequestControlPtr Send(
        IClientRequestPtr request,
        IClientResponseHandlerPtr responseHandler,
        const TSendOptions& options) override
    {
        YT_ASSERT(request);
        YT_ASSERT(responseHandler);

        return New<TRetryingRequest>(
            Config_,
            UnderlyingChannel_,
            std::move(request),
            std::move(responseHandler),
            options,
            IsRetriableError_)
        ->Send();
    }


private:
    const TRetryingChannelConfigPtr Config_;
    const TCallback<bool(const TError&)> IsRetriableError_;


    class TRetryingRequest
        : public IClientResponseHandler
    {
    public:
        TRetryingRequest(
            TRetryingChannelConfigPtr config,
            IChannelPtr underlyingChannel,
            IClientRequestPtr request,
            IClientResponseHandlerPtr responseHandler,
            const TSendOptions& options,
            TCallback<bool(const TError&)> isRetriableError)
            : Config_(std::move(config))
            , UnderlyingChannel_(std::move(underlyingChannel))
            , Request_(std::move(request))
            , ResponseHandler_(std::move(responseHandler))
            , Options_(options)
            , IsRetriableError_(std::move(isRetriableError))
        {
            YT_ASSERT(Config_);
            YT_ASSERT(UnderlyingChannel_);
            YT_ASSERT(Request_);
            YT_ASSERT(ResponseHandler_);

            Deadline_ = Config_->RetryTimeout
                ? TInstant::Now() + *Config_->RetryTimeout
                : TInstant::Max();
        }

        IClientRequestControlPtr Send()
        {
            DoSend();
            return RequestControlThunk_;
        }

    private:
        class TRetryingRequestControlThunk
            : public IClientRequestControl
        {
        public:
            // NB: In contrast to TClientRequestControlThunk::SetUnderlying,
            // this one may be invoked multiple times.
            void SetNewUnderlying(IClientRequestControlPtr newUnderlying)
            {
                VERIFY_THREAD_AFFINITY_ANY();

                if (!newUnderlying) {
                    return;
                }

                SmallVector<IClientRequestControlPtr, 2> toCancelList;

                TGuard<TSpinLock> guard(SpinLock_);

                if (Underlying_) {
                    toCancelList.push_back(std::move(Underlying_));
                }

                if (Canceled_) {
                    toCancelList.push_back(std::move(newUnderlying));
                } else {
                    Underlying_ = std::move(newUnderlying);
                }

                guard.Release();

                for (const auto& toCancel : toCancelList) {
                    toCancel->Cancel();
                }
            }

            virtual void Cancel() override
            {
                VERIFY_THREAD_AFFINITY_ANY();

                IClientRequestControlPtr toCancel;
                TGuard<TSpinLock> guard(SpinLock_);

                Canceled_ = true;
                toCancel = std::move(Underlying_);

                guard.Release();

                if (toCancel) {
                    toCancel->Cancel();
                }
            }

            virtual TFuture<void> SendStreamingPayload(const TStreamingPayload& /*payload*/) override
            {
                VERIFY_THREAD_AFFINITY_ANY();

                return MakeFuture<void>(TError("Retrying channel does not support streaming"));
            }

            virtual TFuture<void> SendStreamingFeedback(const TStreamingFeedback& /*feedback*/) override
            {
                VERIFY_THREAD_AFFINITY_ANY();

                return MakeFuture<void>(TError("Retrying channel does not support streaming"));
            }

        private:
            TSpinLock SpinLock_;
            bool Canceled_ = false;
            IClientRequestControlPtr Underlying_;

        };

        using TRetryingRequestControlThunkPtr = TIntrusivePtr<TRetryingRequestControlThunk>;

        const TRetryingChannelConfigPtr Config_;
        const IChannelPtr UnderlyingChannel_;
        const IClientRequestPtr Request_;
        const IClientResponseHandlerPtr ResponseHandler_;
        const TSendOptions Options_;
        const TCallback<bool(const TError&)> IsRetriableError_;
        const TRetryingRequestControlThunkPtr RequestControlThunk_ = New<TRetryingRequestControlThunk>();

        //! The current attempt number (1-based).
        int CurrentAttempt_ = 1;
        TInstant Deadline_;
        std::vector<TError> InnerErrors_;

        // IClientResponseHandler implementation.

        virtual void HandleAcknowledgement() override
        {
            YT_LOG_DEBUG("Request attempt acknowledged (RequestId: %v)",
                Request_->GetRequestId());

            // NB: The underlying handler is not notified.
        }

        virtual void HandleError(const TError& error) override
        {
            YT_LOG_DEBUG(error, "Request attempt failed (RequestId: %v, Attempt: %v of %v)",
                Request_->GetRequestId(),
                CurrentAttempt_,
                Config_->RetryAttempts);

            if (!IsRetriableError_.Run(error)) {
                ResponseHandler_->HandleError(error);
                return;
            }

            InnerErrors_.push_back(error);
            Retry();
        }

        virtual void HandleResponse(TSharedRefArray message) override
        {
            YT_LOG_DEBUG("Request attempt succeeded (RequestId: %v)",
                Request_->GetRequestId());

            ResponseHandler_->HandleResponse(message);
        }

        virtual void HandleStreamingPayload(const TStreamingPayload& /*payload*/) override
        {
            YT_UNIMPLEMENTED();
        }

        virtual void HandleStreamingFeedback(const TStreamingFeedback& /*feedback*/) override
        {
            YT_UNIMPLEMENTED();
        }


        std::optional<TDuration> ComputeAttemptTimeout(TInstant now)
        {
            auto attemptDeadline = Options_.Timeout ? now + *Options_.Timeout : TInstant::Max();
            auto actualDeadline = std::min(Deadline_, attemptDeadline);
            return actualDeadline == TInstant::Max()
                ? std::optional<TDuration>(std::nullopt)
                : actualDeadline - now;
        }

        void ReportError(const TError& error)
        {
            auto detailedError = error
                << UnderlyingChannel_->GetEndpointAttributes()
                << InnerErrors_;
            ResponseHandler_->HandleError(detailedError);
        }

        void Retry()
        {
            int count = ++CurrentAttempt_;
            if (count > Config_->RetryAttempts || TInstant::Now() + Config_->RetryBackoffTime > Deadline_) {
                ReportError(TError(NRpc::EErrorCode::Unavailable, "Request retries failed"));
                return;
            }

            TDelayedExecutor::Submit(
                BIND(&TRetryingRequest::DoRetry, MakeStrong(this)),
                Config_->RetryBackoffTime);
        }

        void DoRetry(bool aborted)
        {
            if (aborted) {
                ReportError(TError(NYT::EErrorCode::Canceled, "Request timed out (timer was aborted)"));
                return;
            }

            DoSend();
        }

        void DoSend()
        {
            YT_LOG_DEBUG("Request attempt started (RequestId: %v, Method: %v:%v, User: %v, Attempt: %v of %v, RequestTimeout: %v, RetryTimeout: %v)",
                Request_->GetRequestId(),
                Request_->GetService(),
                Request_->GetMethod(),
                Request_->GetUser(),
                CurrentAttempt_,
                Config_->RetryAttempts,
                Options_.Timeout,
                Config_->RetryTimeout);

            auto now = TInstant::Now();
            if (now > Deadline_) {
                ReportError(TError(NYT::EErrorCode::Timeout, "Request retries timed out"));
                return;
            }

            auto adjustedOptions = Options_;
            adjustedOptions.Timeout = ComputeAttemptTimeout(now);
            auto requestControl = UnderlyingChannel_->Send(
                Request_,
                this,
                adjustedOptions);
            RequestControlThunk_->SetNewUnderlying(std::move(requestControl));
        }
    };
};

IChannelPtr CreateRetryingChannel(
    TRetryingChannelConfigPtr config,
    IChannelPtr underlyingChannel,
    TCallback<bool(const TError&)> isRetriableError)
{
    return New<TRetryingChannel>(config, underlyingChannel, isRetriableError);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc
