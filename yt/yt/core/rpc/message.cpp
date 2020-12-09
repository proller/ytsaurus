#include "message.h"
#include "private.h"
#include "service.h"
#include "channel.h"

#include <yt/core/misc/protobuf_helpers.h>

#include <yt/core/rpc/proto/rpc.pb.h>

#include <yt/core/ytalloc/memory_zone.h>

namespace NYT::NRpc {

using namespace NBus;

using NYT::ToProto;
using NYT::FromProto;

////////////////////////////////////////////////////////////////////////////////

#pragma pack(push, 1)

struct TFixedMessageHeader
{
    EMessageType Type;
};

#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////

struct TSerializedMessageTag
{ };

struct TAdjustedMemoryZoneMessageTag
{ };

namespace {

size_t GetAllocationSpaceForProtoWithHeader(const google::protobuf::MessageLite& message)
{
    return
        sizeof (TFixedMessageHeader) +
        message.ByteSize();
}

void SerializeAndAddProtoWithHeader(
    TSharedRefArrayBuilder* builder,
    const TFixedMessageHeader& fixedHeader,
    const google::protobuf::MessageLite& message)
{
    auto ref = builder->AllocateAndAdd(
        sizeof (TFixedMessageHeader) +
        message.GetCachedSize());
    ::memcpy(ref.Begin(), &fixedHeader, sizeof(fixedHeader));
    message.SerializeWithCachedSizesToArray(reinterpret_cast<google::protobuf::uint8*>(ref.Begin() + sizeof(fixedHeader)));
}

size_t GetAllocationSpaceForProtoWithEnvelope(const google::protobuf::MessageLite& message)
{
    return
        sizeof (TEnvelopeFixedHeader) +
        message.ByteSize();
}

void SerializeAndAddProtoWithEnvelope(
    TSharedRefArrayBuilder* builder,
    const google::protobuf::MessageLite& message)
{
    auto ref = builder->AllocateAndAdd(
        sizeof (TEnvelopeFixedHeader) +
        message.GetCachedSize());
    auto* header = static_cast<TEnvelopeFixedHeader*>(static_cast<void*>(ref.Begin()));
    // Empty (default) TSerializedMessageEnvelope.
    header->EnvelopeSize = 0;
    header->MessageSize = message.GetCachedSize();
    message.SerializeWithCachedSizesToArray(reinterpret_cast<google::protobuf::uint8*>(ref.Begin() + sizeof(TEnvelopeFixedHeader)));
}

bool DeserializeFromProtoWithHeader(
    google::protobuf::MessageLite* message,
    TRef data)
{
    if (data.Size() < sizeof(TFixedMessageHeader)) {
        return false;
    }
    return message->ParsePartialFromArray(
        data.Begin() + sizeof(TFixedMessageHeader),
        data.Size() - sizeof(TFixedMessageHeader));
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

TSharedRefArray CreateRequestMessage(
    const NProto::TRequestHeader& header,
    TSharedRef body,
    const std::vector<TSharedRef>& attachments)
{
    TSharedRefArrayBuilder builder(
        2 + attachments.size(),
        GetAllocationSpaceForProtoWithHeader(header),
        GetRefCountedTypeCookie<TSerializedMessageTag>());
    SerializeAndAddProtoWithHeader(
        &builder,
        TFixedMessageHeader{EMessageType::Request},
        header);
    builder.Add(std::move(body));
    for (auto attachment : attachments) {
        builder.Add(std::move(attachment));
    }
    return builder.Finish();
}

TSharedRefArray CreateRequestMessage(
    const NProto::TRequestHeader& header,
    const TSharedRefArray& data)
{
    TSharedRefArrayBuilder builder(
        1 + data.Size(),
        GetAllocationSpaceForProtoWithHeader(header),
        GetRefCountedTypeCookie<TSerializedMessageTag>());
    SerializeAndAddProtoWithHeader(
        &builder,
        TFixedMessageHeader{EMessageType::Request},
        header);
    for (auto part : data) {
        builder.Add(std::move(part));
    }
    return builder.Finish();
}

TSharedRefArray CreateRequestCancelationMessage(
    const NProto::TRequestCancelationHeader& header)
{
    TSharedRefArrayBuilder builder(
        1,
        GetAllocationSpaceForProtoWithHeader(header),
        GetRefCountedTypeCookie<TSerializedMessageTag>());
    SerializeAndAddProtoWithHeader(
        &builder,
        TFixedMessageHeader{EMessageType::RequestCancelation},
        header);
    return builder.Finish();
}

TSharedRefArray CreateResponseMessage(
    const NProto::TResponseHeader& header,
    TSharedRef body,
    const std::vector<TSharedRef>& attachments)
{
    TSharedRefArrayBuilder builder(
        2 + attachments.size(),
        GetAllocationSpaceForProtoWithHeader(header),
        GetRefCountedTypeCookie<TSerializedMessageTag>());
    SerializeAndAddProtoWithHeader(
        &builder,
        TFixedMessageHeader{EMessageType::Response},
        header);
    builder.Add(std::move(body));
    for (auto attachment : attachments) {
        builder.Add(std::move(attachment));
    }
    return builder.Finish();
}

TSharedRefArray CreateResponseMessage(
    const ::google::protobuf::MessageLite& body,
    const std::vector<TSharedRef>& attachments)
{
    NProto::TResponseHeader header;
    TSharedRefArrayBuilder builder(
        2 + attachments.size(),
        GetAllocationSpaceForProtoWithHeader(header) + GetAllocationSpaceForProtoWithEnvelope(body),
        GetRefCountedTypeCookie<TSerializedMessageTag>());
    SerializeAndAddProtoWithHeader(
        &builder,
        TFixedMessageHeader{EMessageType::Response},
        header);
    SerializeAndAddProtoWithEnvelope(
        &builder,
        body);
    for (auto attachment : attachments) {
        builder.Add(std::move(attachment));
    }
    return builder.Finish();
}

TSharedRefArray CreateErrorResponseMessage(
    const NProto::TResponseHeader& header)
{
    TSharedRefArrayBuilder builder(
        1,
        GetAllocationSpaceForProtoWithHeader(header),
        GetRefCountedTypeCookie<TSerializedMessageTag>());
    SerializeAndAddProtoWithHeader(
        &builder,
        TFixedMessageHeader{EMessageType::Response},
        header);
    return builder.Finish();
}

TSharedRefArray CreateErrorResponseMessage(
    TRequestId requestId,
    const TError& error)
{
    NProto::TResponseHeader header;
    ToProto(header.mutable_request_id(), requestId);
    if (!error.IsOK()) {
        ToProto(header.mutable_error(), error);
    }
    return CreateErrorResponseMessage(header);
}

TSharedRefArray CreateErrorResponseMessage(
    const TError& error)
{
    NProto::TResponseHeader header;
    if (!error.IsOK()) {
        ToProto(header.mutable_error(), error);
    }
    return CreateErrorResponseMessage(header);
}

TSharedRefArray CreateStreamingPayloadMessage(
    const NProto::TStreamingPayloadHeader& header,
    const std::vector<TSharedRef>& attachments)
{
    TSharedRefArrayBuilder builder(
        1 + attachments.size(),
        GetAllocationSpaceForProtoWithHeader(header),
        GetRefCountedTypeCookie<TSerializedMessageTag>());
    SerializeAndAddProtoWithHeader(
        &builder,
        TFixedMessageHeader{EMessageType::StreamingPayload},
        header);
    for (auto attachment : attachments) {
        builder.Add(std::move(attachment));
    }
    return builder.Finish();
}

TSharedRefArray CreateStreamingFeedbackMessage(
    const NProto::TStreamingFeedbackHeader& header)
{
    TSharedRefArrayBuilder builder(
        1,
        GetAllocationSpaceForProtoWithHeader(header),
        GetRefCountedTypeCookie<TSerializedMessageTag>());
    SerializeAndAddProtoWithHeader(
        &builder,
        TFixedMessageHeader{EMessageType::StreamingFeedback},
        header);
    return builder.Finish();
}

TSharedRefArray AdjustMessageMemoryZone(
    TSharedRefArray message,
    NYTAlloc::EMemoryZone memoryZone)
{
    bool copyNeeded = false;
    for (int index = 2; index < message.Size(); ++index) {
        const auto& part = message[index];
        if (part.Size() > 0 && NYTAlloc::GetAllocationMemoryZone(part.Begin()) != memoryZone) {
            copyNeeded = true;
            break;
        }
    }

    if (!copyNeeded) {
        return message;
    }

    TSharedRefArrayBuilder builder(message.Size());

    for (int index = 0; index < std::min(static_cast<int>(message.Size()), 2); ++index) {
        builder.Add(message[index]);
    }

    for (int index = 2; index < message.Size(); ++index) {
        const auto& part = message[index];
        if (part.Size() > 0 && NYTAlloc::GetAllocationMemoryZone(part.Begin()) != memoryZone) {
            NYTAlloc::TMemoryZoneGuard guard(memoryZone);
            auto copiedPart = TSharedMutableRef::Allocate<TAdjustedMemoryZoneMessageTag>(part.Size(), false);
            ::memcpy(copiedPart.Begin(), part.Begin(), part.Size());
            builder.Add(std::move(copiedPart));
        } else {
            builder.Add(part);
        }
    }

    return builder.Finish();
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(
    NProto::TStreamingParameters* protoParameters,
    const TStreamingParameters& parameters)
{
    protoParameters->set_window_size(parameters.WindowSize);
    if (parameters.ReadTimeout) {
        protoParameters->set_read_timeout(ToProto<i64>(*parameters.ReadTimeout));
    }
    if (parameters.WriteTimeout) {
        protoParameters->set_write_timeout(ToProto<i64>(*parameters.WriteTimeout));
    }
}

void FromProto(
    TStreamingParameters* parameters,
    const NProto::TStreamingParameters& protoParameters)
{
    if (protoParameters.has_window_size()) {
        parameters->WindowSize = protoParameters.window_size();
    }
    if (protoParameters.has_read_timeout()) {
        parameters->ReadTimeout = FromProto<TDuration>(protoParameters.read_timeout());
    }
    if (protoParameters.has_write_timeout()) {
        parameters->WriteTimeout = FromProto<TDuration>(protoParameters.write_timeout());
    }
}

////////////////////////////////////////////////////////////////////////////////

EMessageType GetMessageType(const TSharedRefArray& message)
{
    if (message.Size() < 1) {
        return EMessageType::Unknown;
    }

    const auto& headerPart = message[0];
    if (headerPart.Size() < sizeof(TFixedMessageHeader)) {
        return EMessageType::Unknown;
    }

    const auto* header = reinterpret_cast<const TFixedMessageHeader*>(headerPart.Begin());
    return header->Type;
}

bool ParseRequestHeader(
    const TSharedRefArray& message,
    NProto::TRequestHeader* header)
{
    if (GetMessageType(message) != EMessageType::Request) {
        return false;
    }

    return DeserializeFromProtoWithHeader(header, message[0]);
}

TSharedRefArray SetRequestHeader(
    const TSharedRefArray& message,
    const NProto::TRequestHeader& header)
{
    YT_ASSERT(GetMessageType(message) == EMessageType::Request);
    TSharedRefArrayBuilder builder(
        message.Size(),
        GetAllocationSpaceForProtoWithHeader(header),
        GetRefCountedTypeCookie<TSerializedMessageTag>());
    SerializeAndAddProtoWithHeader(
        &builder,
        TFixedMessageHeader{EMessageType::Request},
        header);
    for (size_t index = 1; index < message.Size(); ++index) {
        builder.Add(message[index]);
    }
    return builder.Finish();
}

bool TryParseResponseHeader(
    const TSharedRefArray& message,
    NProto::TResponseHeader* header)
{
    if (GetMessageType(message) != EMessageType::Response) {
        return false;
    }

    return DeserializeFromProtoWithHeader(header, message[0]);
}

TSharedRefArray SetResponseHeader(
    const TSharedRefArray& message,
    const NProto::TResponseHeader& header)
{
    YT_ASSERT(GetMessageType(message) == EMessageType::Response);
    TSharedRefArrayBuilder builder(
        message.Size(),
        GetAllocationSpaceForProtoWithHeader(header),
        GetRefCountedTypeCookie<TSerializedMessageTag>());
    SerializeAndAddProtoWithHeader(
        &builder,
        TFixedMessageHeader{EMessageType::Response},
        header);
    for (size_t index = 1; index < message.Size(); ++index) {
        builder.Add(message[index]);
    }
    return builder.Finish();
}

void MergeRequestHeaderExtensions(
    NProto::TRequestHeader* to,
    const NProto::TRequestHeader& from)
{
#define XX(name) \
    if (from.HasExtension(name)) { \
        to->MutableExtension(name)->CopyFrom(from.GetExtension(name)); \
    }

    XX(NRpc::NProto::TRequestHeader::tracing_ext)

#undef XX
}

bool ParseRequestCancelationHeader(
    const TSharedRefArray& message,
    NProto::TRequestCancelationHeader* header)
{
    if (GetMessageType(message) != EMessageType::RequestCancelation) {
        return false;
    }

    return DeserializeFromProtoWithHeader(header, message[0]);
}

bool ParseStreamingPayloadHeader(
    const TSharedRefArray& message,
    NProto::TStreamingPayloadHeader * header)
{
    if (GetMessageType(message) != EMessageType::StreamingPayload) {
        return false;
    }

    return DeserializeFromProtoWithHeader(header, message[0]);
}

bool ParseStreamingFeedbackHeader(
    const TSharedRefArray& message,
    NProto::TStreamingFeedbackHeader * header)
{
    if (GetMessageType(message) != EMessageType::StreamingFeedback) {
        return false;
    }

    return DeserializeFromProtoWithHeader(header, message[0]);
}

////////////////////////////////////////////////////////////////////////////////

i64 GetMessageBodySize(const TSharedRefArray& message)
{
    return message.Size() >= 2 ? static_cast<i64>(message[1].Size()) : 0;
}

int GetMessageAttachmentCount(const TSharedRefArray& message)
{
    return std::max(static_cast<int>(message.Size()) - 2, 0);
}

i64 GetTotalMessageAttachmentSize(const TSharedRefArray& message)
{
    i64 result = 0;
    for (int index = 2; index < message.Size(); ++index) {
        result += message[index].Size();
    }
    return result;
}

TError CheckBusMessageLimits(const TSharedRefArray& message)
{
    if (message.Size() > NBus::MaxMessagePartCount) {
        return TError(
            NRpc::EErrorCode::TransportError,
            "RPC message contains too many attachments: %v > %v",
            message.Size() - 2,
            NBus::MaxMessagePartCount - 2);
    }

    if (message.Size() < 2) {
        return TError();
    }

    if (message[1].Size() > NBus::MaxMessagePartSize) {
        return TError(
            NRpc::EErrorCode::TransportError,
            "RPC message body is too large: %v > %v",
            message[1].Size(),
            NBus::MaxMessagePartSize);
    }

    for (size_t index = 2; index < message.Size(); ++index) {
        if (message[index].Size() > NBus::MaxMessagePartSize) {
            return TError(
                NRpc::EErrorCode::TransportError,
                "RPC message attachment %v is too large: %v > %v",
                index - 2,
                message[index].Size(),
                NBus::MaxMessagePartSize);
        }
    }

    return TError();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc
