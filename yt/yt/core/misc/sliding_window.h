#pragma once

#include "error.h"
#include "public.h"

#include <yt/yt/core/actions/callback.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

//! A classic sliding window implementation.
/*!
 *  Can defer up to #windowSize "packets" (abstract movable objects) and reorder
 *  them according to their sequence numbers. If #windowSize is not set, it's
 *  assumed to be infinity.
 *
 *  Once a packet is received from the outside world, the user should call
 *  #AddPacket, providing packet's sequence number.
 *
 *  The #callback is called once for each packet when it's about to be popped
 *  out of the window. Specifically, a packet leaves the window when no
 *  packets preceding it are missing.
 *
 *  #callback mustn't throw.
 */
////////////////////////////////////////////////////////////////////////////////

template <typename TPacket>
class TSlidingWindow
{
public:
    //! Constructs the sliding window.
    explicit TSlidingWindow(ssize_t maxSize);

    //! Informs the window that the packet has been received.
    /*!
    *  May cause #callback to be called for deferred packets (up to
    *  #WindowSize times).
    *
    *  Throws if a packet with the specified sequence number has already been
    *  set.
    *  Throws if the sequence number was already slid over (i.e. it's too
    *  small).
    *  Throws if setting this packet would exceed the window size (i.e. the
    *  sequence number is too large).
    */
    template <class F>
    void AddPacket(ssize_t sequenceNumber, TPacket&& packet, const F& callback);

    //! Checks whether the window stores no packets.
    bool IsEmpty() const;

    //! Returns the first missing sequence number.
    ssize_t GetNextSequenceNumber() const;

private:
    const ssize_t MaxSize_;

    ssize_t NextPacketSequenceNumber_ = 0;
    THashMap<ssize_t, TPacket> Window_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

#define SLIDING_WINDOW_INL_H_
#include "sliding_window-inl.h"
#undef SLIDING_WINDOW_INL_H_
