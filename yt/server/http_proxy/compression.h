#pragma once

#include "public.h"

#include <yt/core/concurrency/async_stream.h>

namespace NYT {
namespace NHttpProxy {

////////////////////////////////////////////////////////////////////////////////

class TSharedRefOutputStream
    : public NConcurrency::IAsyncOutputStream
{
public:
    virtual TFuture<void> Write(const TSharedRef& buffer) override;
    virtual TFuture<void> Close() override;

    const std::vector<TSharedRef>& GetRefs() const;

private:
    std::vector<TSharedRef> Refs_;
};


DEFINE_REFCOUNTED_TYPE(TSharedRefOutputStream)

////////////////////////////////////////////////////////////////////////////////

bool IsCompressionSupported(const TContentEncoding& contentEncoding);

extern TContentEncoding IdentityContentEncoding;

TErrorOr<TContentEncoding> GetBestAcceptedEncoding(const TString& clientAcceptEncodingHeader);

NConcurrency::IAsyncOutputStreamPtr CreateCompressingAdapter(
    NConcurrency::IAsyncOutputStreamPtr underlying,
    TContentEncoding contentEncoding);

NConcurrency::IAsyncInputStreamPtr CreateDecompressingAdapter(
    NConcurrency::IAsyncZeroCopyInputStreamPtr underlying,
    TContentEncoding contentEncoding);

////////////////////////////////////////////////////////////////////////////////

} // namespace NHttpProxy
} // namespace NYT
