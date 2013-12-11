#pragma once

#include "public.h"

#include <core/misc/nullable.h>
#include <core/misc/property.h>
#include <core/misc/error.h>

#include <core/bus/client.h>

namespace NYT {
namespace NRpc {

////////////////////////////////////////////////////////////////////////////////

//! An interface for exchanging request-response pairs.
/*!
 * \note Thread affinity: any.
 */
struct IChannel
    : public virtual TRefCounted
{
    //! Gets the default timeout.
    virtual TNullable<TDuration> GetDefaultTimeout() const = 0;

    //! Sets the default timeout.
    virtual void SetDefaultTimeout(const TNullable<TDuration>& timeout) = 0;

    //! Sends a request via the channel.
    /*!
     *  \param request A request to send.
     *  \param responseHandler An object that will handle a response.
     *  \param timeout Request processing timeout.
     */
    virtual void Send(
        IClientRequestPtr request,
        IClientResponseHandlerPtr responseHandler,
        TNullable<TDuration> timeout) = 0;

    //! Shuts down the channel.
    /*!
     *  It is safe to call this method multiple times.
     *  After the first call the instance is no longer usable.
     */
    virtual TFuture<void> Terminate(const TError& error) = 0;
};

//! Provides means for parsing addresses and creating channels.
struct IChannelFactory
    : public virtual TRefCounted
{
    virtual IChannelPtr CreateChannel(const Stroka& address) = 0;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc
} // namespace NYT
