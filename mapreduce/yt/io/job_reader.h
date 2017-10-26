#pragma once

#include "proxy_input.h"

#include <util/stream/buffered.h>
#include <util/stream/file.h>
#include <util/system/file.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class TJobReader
    : public TProxyInput
{
public:
    explicit TJobReader(int fd);

protected:
    size_t DoRead(void* buf, size_t len) override;

private:
    int Fd_;
    TFile FdFile_;
    TUnbufferedFileInput FdInput_;
    TBufferedInput BufferedInput_;

    static const size_t BUFFER_SIZE = 64 << 10;
};

////////////////////////////////////////////////////////////////////////////////


} // namespace NYT
