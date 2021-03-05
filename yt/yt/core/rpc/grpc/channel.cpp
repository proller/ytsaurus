#include "channel.h"
#include "config.h"
#include "dispatcher.h"
#include "helpers.h"

#include <yt/yt/core/misc/singleton.h>
#include <yt/yt/core/misc/finally.h>

#include <yt/yt/core/rpc/channel.h>
#include <yt/yt/core/rpc/message.h>
#include <yt/yt/core/rpc/proto/rpc.pb.h>

#include <yt/yt/core/ytree/fluent.h>

#include <yt/yt/core/concurrency/spinlock.h>

#include <yt/yt/core/profiling/timing.h>

#include <contrib/libs/grpc/include/grpc/grpc.h>

#include <array>

namespace NYT::NRpc::NGrpc {

using namespace NRpc;
using namespace NYTree;
using namespace NYson;
using namespace NConcurrency;
using namespace NBus;

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TChannel)

DEFINE_ENUM(EClientCallStage,
    (SendingRequest)
    (ReceivingInitialMetadata)
    (ReceivingResponse)
);

class TChannel
    : public IChannel
{
public:
    explicit TChannel(TChannelConfigPtr config)
        : Config_(std::move(config))
        , EndpointDescription_(Config_->Address)
        , EndpointAttributes_(ConvertToAttributes(BuildYsonStringFluently()
            .BeginMap()
                .Item("address").Value(EndpointDescription_)
            .EndMap()))
    {
        TGrpcChannelArgs args(Config_->GrpcArguments);
        if (Config_->Credentials) {
            Credentials_ = LoadChannelCredentials(Config_->Credentials);
            Channel_ = TGrpcChannelPtr(grpc_secure_channel_create(
                Credentials_.Unwrap(),
                Config_->Address.c_str(),
                args.Unwrap(),
                nullptr));
        } else {
            Channel_ = TGrpcChannelPtr(grpc_insecure_channel_create(
                Config_->Address.c_str(),
                args.Unwrap(),
                nullptr));
        }
    }

    virtual const TString& GetEndpointDescription() const override
    {
        return EndpointDescription_;
    }

    virtual const IAttributeDictionary& GetEndpointAttributes() const override
    {
        return *EndpointAttributes_;
    }

    virtual TNetworkId GetNetworkId() const override
    {
        return DefaultNetworkId;
    }

    virtual IClientRequestControlPtr Send(
        IClientRequestPtr request,
        IClientResponseHandlerPtr responseHandler,
        const TSendOptions& options) override
    {
        auto guard = ReaderGuard(SpinLock_);
        if (!TerminationError_.IsOK()) {
            auto error = TerminationError_;
            guard.Release();
            responseHandler->HandleError(error);
            return nullptr;
        }
        return New<TCallHandler>(
            this,
            options,
            std::move(request),
            std::move(responseHandler));
    }

    virtual void Terminate(const TError& error) override
    {
        {
            auto guard = WriterGuard(SpinLock_);

            if (!TerminationError_.IsOK()) {
                return;
            }

            TerminationError_ = error;
            LibraryLock_.Reset();
            Channel_.Reset();
        }

        Terminated_.Fire(TerminationError_);
    }

    virtual void SubscribeTerminated(const TCallback<void(const TError&)>& callback) override
    {
        Terminated_.Subscribe(callback);
    }

    virtual void UnsubscribeTerminated(const TCallback<void(const TError&)>& callback) override
    {
        Terminated_.Unsubscribe(callback);
    }

private:
    const TChannelConfigPtr Config_;
    const TString EndpointDescription_;
    const IAttributeDictionaryPtr EndpointAttributes_;

    TSingleShotCallbackList<void(const TError&)> Terminated_;

    YT_DECLARE_SPINLOCK(TReaderWriterSpinLock, SpinLock_);
    TError TerminationError_;
    TGrpcLibraryLockPtr LibraryLock_ = TDispatcher::Get()->CreateLibraryLock();
    TGrpcChannelPtr Channel_;
    TGrpcChannelCredentialsPtr Credentials_;


    class TCallHandler
        : public TCompletionQueueTag
        , public IClientRequestControl
    {
    public:
        TCallHandler(
            TChannelPtr owner,
            const TSendOptions& options,
            IClientRequestPtr request,
            IClientResponseHandlerPtr responseHandler)
            : Owner_(std::move(owner))
            , Options_(options)
            , Request_(std::move(request))
            , ResponseHandler_(std::move(responseHandler))
            , CompletionQueue_(TDispatcher::Get()->PickRandomCompletionQueue())
            , Logger(GrpcLogger)
        {
            YT_LOG_DEBUG("Sending request (RequestId: %v, Method: %v.%v, Timeout: %v)",
                Request_->GetRequestId(),
                Request_->GetService(),
                Request_->GetMethod(),
                Options_.Timeout);

            auto methodSlice = BuildGrpcMethodString();
            Call_ = TGrpcCallPtr(grpc_channel_create_call(
                Owner_->Channel_.Unwrap(),
                nullptr,
                0,
                CompletionQueue_,
                methodSlice,
                nullptr,
                GetDeadline(),
                nullptr));
            grpc_slice_unref(methodSlice);

            InitialMetadataBuilder_.Add(RequestIdMetadataKey, ToString(Request_->GetRequestId()));
            InitialMetadataBuilder_.Add(UserMetadataKey, Request_->GetUser());
            if (Request_->GetUserTag()) {
                InitialMetadataBuilder_.Add(UserTagMetadataKey, Request_->GetUserTag());
            }

            TProtocolVersion protocolVersion{
                Request_->Header().protocol_version_major(),
                Request_->Header().protocol_version_minor()
            };

            InitialMetadataBuilder_.Add(ProtocolVersionMetadataKey, ToString(protocolVersion));

            if (Request_->Header().HasExtension(NRpc::NProto::TCredentialsExt::credentials_ext)) {
                const auto& credentialsExt = Request_->Header().GetExtension(NRpc::NProto::TCredentialsExt::credentials_ext);
                if (credentialsExt.has_token()) {
                    InitialMetadataBuilder_.Add(AuthTokenMetadataKey, credentialsExt.token());
                }
                if (credentialsExt.has_session_id()) {
                    InitialMetadataBuilder_.Add(AuthSessionIdMetadataKey, credentialsExt.session_id());
                }
                if (credentialsExt.has_ssl_session_id()) {
                    InitialMetadataBuilder_.Add(AuthSslSessionIdMetadataKey, credentialsExt.ssl_session_id());
                }
                if (credentialsExt.has_user_ticket()) {
                    InitialMetadataBuilder_.Add(AuthUserTicketMetadataKey, credentialsExt.user_ticket());
                }
            }

            try {
                RequestBody_ = Request_->Serialize();
            } catch (const std::exception& ex) {
                auto responseHandler = TryAcquireResponseHandler();
                YT_VERIFY(responseHandler);
                responseHandler->HandleError(TError(NRpc::EErrorCode::TransportError, "Request serialization failed")
                    << ex);
                return;
            }

            YT_VERIFY(RequestBody_.Size() >= 2);
            TMessageWithAttachments messageWithAttachments;
            if (Request_->IsLegacyRpcCodecsEnabled()) {
                messageWithAttachments.Message = ExtractMessageFromEnvelopedMessage(RequestBody_[1]);
            } else {
                messageWithAttachments.Message = RequestBody_[1];
            }

            for (int index = 2; index < RequestBody_.Size(); ++index) {
                messageWithAttachments.Attachments.push_back(RequestBody_[index]);
            }

            RequestBodyBuffer_ = MessageWithAttachmentsToByteBuffer(messageWithAttachments);
            if (!messageWithAttachments.Attachments.empty()) {
                InitialMetadataBuilder_.Add(MessageBodySizeMetadataKey, ToString(messageWithAttachments.Message.Size()));
            }

            Ref();
            Stage_ = EClientCallStage::SendingRequest;

            std::array<grpc_op, 3> ops;

            ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
            ops[0].flags = 0;
            ops[0].reserved = nullptr;
            ops[0].data.send_initial_metadata.maybe_compression_level.is_set = false;
            ops[0].data.send_initial_metadata.metadata = InitialMetadataBuilder_.Unwrap();
            ops[0].data.send_initial_metadata.count = InitialMetadataBuilder_.GetSize();

            ops[1].op = GRPC_OP_SEND_MESSAGE;
            ops[1].data.send_message.send_message = RequestBodyBuffer_.Unwrap();
            ops[1].flags = 0;
            ops[1].reserved = nullptr;

            ops[2].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
            ops[2].flags = 0;
            ops[2].reserved = nullptr;

            StartBatch(ops);
        }

        ~TCallHandler()
        {
            grpc_slice_unref(ResponseStatusDetails_);
        }

        // TCompletionQueueTag overrides
        virtual void Run(bool success, int /*cookie*/) override
        {
            switch (Stage_) {
                case EClientCallStage::SendingRequest:
                    OnRequestSent(success);
                    break;

                case EClientCallStage::ReceivingInitialMetadata:
                    OnInitialMetadataReceived(success);
                    break;

                case EClientCallStage::ReceivingResponse:
                    OnResponseReceived(success);
                    break;

                default:
                    YT_ABORT();
            }
        }

        // IClientRequestControl overrides
        virtual void Cancel() override
        {
            auto result = grpc_call_cancel(Call_.Unwrap(), nullptr);
            YT_VERIFY(result == GRPC_CALL_OK);

            YT_LOG_DEBUG("Request canceled (RequestId: %v)", Request_->GetRequestId());

            NotifyError(
                TStringBuf("Request canceled"),
                TError(NYT::EErrorCode::Canceled, "Request canceled"));
        }

        virtual TFuture<void> SendStreamingPayload(const TStreamingPayload& /*payload*/) override
        {
            YT_UNIMPLEMENTED();
        }

        virtual TFuture<void> SendStreamingFeedback(const TStreamingFeedback& /*feedback*/) override
        {
            YT_UNIMPLEMENTED();
        }

    private:
        const TChannelPtr Owner_;
        const TSendOptions Options_;
        const IClientRequestPtr Request_;

        YT_DECLARE_SPINLOCK(TAdaptiveLock, ResponseHandlerLock_);
        IClientResponseHandlerPtr ResponseHandler_;

        grpc_completion_queue* const CompletionQueue_;
        const NLogging::TLogger& Logger;

        NProfiling::TWallTimer Timer_;

        TGrpcCallPtr Call_;
        TSharedRefArray RequestBody_;
        TGrpcByteBufferPtr RequestBodyBuffer_;
        TGrpcMetadataArray ResponseInitialMetadata_;
        TGrpcByteBufferPtr ResponseBodyBuffer_;
        TGrpcMetadataArray ResponseFinalMetadata_;
        grpc_status_code ResponseStatusCode_ = GRPC_STATUS_UNKNOWN;
        grpc_slice ResponseStatusDetails_ = grpc_empty_slice();

        EClientCallStage Stage_;

        TGrpcMetadataArrayBuilder InitialMetadataBuilder_;


        IClientResponseHandlerPtr TryAcquireResponseHandler()
        {
            IClientResponseHandlerPtr result;

            auto guard = Guard(ResponseHandlerLock_);

            // NB! Reset response handler explicitly.
            // Implicit destruction in ~TCallHandler cannot be guaranteed
            // because of the possible cycle dependency between call handler and
            // response handler, for example for retrying channels.
            result.Swap(ResponseHandler_);

            return result;
        }

        //! Builds /<service>/<method> string.
        grpc_slice BuildGrpcMethodString()
        {
            auto length =
                1 + // slash
                Request_->GetService().length() +
                1 + // slash
                Request_->GetMethod().length();
            auto slice = grpc_slice_malloc(length);
            auto* ptr = GRPC_SLICE_START_PTR(slice);
            *ptr++ = '/';
            ::memcpy(ptr, Request_->GetService().c_str(), Request_->GetService().length());
            ptr += Request_->GetService().length();
            *ptr++ = '/';
            ::memcpy(ptr, Request_->GetMethod().c_str(), Request_->GetMethod().length());
            ptr += Request_->GetMethod().length();
            YT_ASSERT(ptr == GRPC_SLICE_END_PTR(slice));
            return slice;
        }

        gpr_timespec GetDeadline() const
        {
            return Options_.Timeout
                ? gpr_time_add(
                    gpr_now(GPR_CLOCK_REALTIME),
                    gpr_time_from_micros(Options_.Timeout->MicroSeconds(), GPR_TIMESPAN))
                : gpr_inf_future(GPR_CLOCK_REALTIME);
        }

        void OnRequestSent(bool success)
        {
            if (!success) {
                NotifyError(
                    TStringBuf("Failed to send request"),
                    TError(NRpc::EErrorCode::TransportError, "Failed to send request"));
                Unref();
                return;
            }

            YT_LOG_DEBUG("Request sent (RequestId: %v, Method: %v.%v)",
                Request_->GetRequestId(),
                Request_->GetService(),
                Request_->GetMethod());

            Stage_ = EClientCallStage::ReceivingInitialMetadata;

            std::array<grpc_op, 1> ops;

            ops[0].op = GRPC_OP_RECV_INITIAL_METADATA;
            ops[0].flags = 0;
            ops[0].reserved = nullptr;
            ops[0].data.recv_initial_metadata.recv_initial_metadata = ResponseInitialMetadata_.Unwrap();

            StartBatch(ops);
        }

        void OnInitialMetadataReceived(bool success)
        {
            if (!success) {
                NotifyError(
                    TStringBuf("Failed to receive initial response metadata"),
                    TError(NRpc::EErrorCode::TransportError, "Failed to receive initial response metadata"));
                Unref();
                return;
            }

            YT_LOG_DEBUG("Initial response metadata received (RequestId: %v)",
                Request_->GetRequestId());

            Stage_ = EClientCallStage::ReceivingResponse;

            std::array<grpc_op, 2> ops;

            ops[0].op = GRPC_OP_RECV_MESSAGE;
            ops[0].flags = 0;
            ops[0].reserved = nullptr;
            ops[0].data.recv_message.recv_message = ResponseBodyBuffer_.GetPtr();

            ops[1].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
            ops[1].flags = 0;
            ops[1].reserved = nullptr;
            ops[1].data.recv_status_on_client.trailing_metadata = ResponseFinalMetadata_.Unwrap();
            ops[1].data.recv_status_on_client.status = &ResponseStatusCode_;
            ops[1].data.recv_status_on_client.status_details = &ResponseStatusDetails_;
            ops[1].data.recv_status_on_client.error_string = nullptr;

            StartBatch(ops);
        }

        void OnResponseReceived(bool success)
        {
            auto guard = Finally([this] { Unref(); });

            if (!success) {
                NotifyError(
                    TStringBuf("Failed to receive response"),
                    TError(NRpc::EErrorCode::TransportError, "Failed to receive response"));
                return;
            }

            if (ResponseStatusCode_ != GRPC_STATUS_OK) {
                TError error;
                auto serializedError = ResponseFinalMetadata_.Find(ErrorMetadataKey);
                if (serializedError) {
                    error = DeserializeError(serializedError);
                } else {
                    error = TError(StatusCodeToErrorCode(ResponseStatusCode_), ToString(ResponseStatusDetails_))
                        << TErrorAttribute("status_code", ResponseStatusCode_);
                }
                NotifyError(TStringBuf("Request failed"), error);
                return;
            }

            if (!ResponseBodyBuffer_) {
                auto error = TError(NRpc::EErrorCode::ProtocolError, "Empty response body");
                NotifyError(TStringBuf("Request failed"), error);
                return;
            }

            std::optional<ui32> messageBodySize;

            auto messageBodySizeString = ResponseFinalMetadata_.Find(MessageBodySizeMetadataKey);
            if (messageBodySizeString) {
                try {
                    messageBodySize = FromString<ui32>(messageBodySizeString);
                } catch (const std::exception& ex) {
                    auto error = TError(NRpc::EErrorCode::TransportError, "Failed to parse response message body size")
                        << ex;
                    NotifyError(TStringBuf("Failed to parse response message body size"), error);
                    return;
                }
            }

            TMessageWithAttachments messageWithAttachments;
            try {
                messageWithAttachments = ByteBufferToMessageWithAttachments(
                    ResponseBodyBuffer_.Unwrap(),
                    messageBodySize);
            } catch (const std::exception& ex) {
                auto error = TError(NRpc::EErrorCode::TransportError, "Failed to receive request body") << ex;
                NotifyError(TStringBuf("Failed to receive request body"), error);
                return;
            }

            NRpc::NProto::TResponseHeader responseHeader;
            ToProto(responseHeader.mutable_request_id(), Request_->GetRequestId());

            auto responseMessage = CreateResponseMessage(
                responseHeader,
                messageWithAttachments.Message,
                messageWithAttachments.Attachments);

            NotifyResponse(std::move(responseMessage));
        }


        template <class TOps>
        void StartBatch(const TOps& ops)
        {
            auto result = grpc_call_start_batch(
                Call_.Unwrap(),
                ops.data(),
                ops.size(),
                GetTag(),
                nullptr);
            YT_VERIFY(result == GRPC_CALL_OK);
        }

        void NotifyError(TStringBuf reason, const TError& error)
        {
            auto responseHandler = TryAcquireResponseHandler();
            if (!responseHandler) {
                return;
            }

            auto detailedError = error
                << TErrorAttribute("realm_id", Request_->GetRealmId())
                << TErrorAttribute("service", Request_->GetService())
                << TErrorAttribute("method", Request_->GetMethod())
                << TErrorAttribute("request_id", Request_->GetRequestId())
                << Owner_->GetEndpointAttributes();
            if (Options_.Timeout) {
                detailedError = detailedError
                    << TErrorAttribute("timeout", Options_.Timeout);
            }

            YT_LOG_DEBUG(detailedError, "%v (RequestId: %v)",
                reason,
                Request_->GetRequestId());

            responseHandler->HandleError(detailedError);
        }

        void NotifyResponse(TSharedRefArray message)
        {
            auto responseHandler = TryAcquireResponseHandler();
            if (!responseHandler) {
                return;
            }

            YT_LOG_DEBUG("Response received (RequestId: %v, Method: %v.%v, TotalTime: %v)",
                Request_->GetRequestId(),
                Request_->GetService(),
                Request_->GetMethod(),
                Timer_.GetElapsedTime());

            responseHandler->HandleResponse(std::move(message));
        }
    };
};

DEFINE_REFCOUNTED_TYPE(TChannel)

IChannelPtr CreateGrpcChannel(TChannelConfigPtr config)
{
    return New<TChannel>(std::move(config));
}

////////////////////////////////////////////////////////////////////////////////

class TChannelFactory
    : public IChannelFactory
{
public:
    virtual IChannelPtr CreateChannel(const TString& address) override
    {
        auto config = New<TChannelConfig>();
        config->Address = address;
        return CreateGrpcChannel(config);
    }

    virtual IChannelPtr CreateChannel(const TAddressWithNetwork& addressWithNetwork) override
    {
        return CreateChannel(addressWithNetwork.Address);
    }
};

IChannelFactoryPtr GetGrpcChannelFactory()
{
    return RefCountedSingleton<TChannelFactory>();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc::NGrpc
