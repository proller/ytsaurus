#pragma once

#include "command.h"

namespace NYT {
namespace NDriver {

////////////////////////////////////////////////////////////////////////////////

struct TDownloadRequest
    : public TTransactedRequest
{
    NYTree::TYPath Path;

    TDownloadRequest()
    {
        Register("path", Path);
    }
};

typedef TIntrusivePtr<TDownloadRequest> TDownloadRequestPtr;

class TDownloadCommand
    : public TCommandBase<TDownloadRequest>
{
public:
    TDownloadCommand(ICommandHost* commandHost)
        : TCommandBase(commandHost)
    { }

private:
    virtual void DoExecute(TDownloadRequestPtr request);
};

////////////////////////////////////////////////////////////////////////////////

struct TUploadRequest
    : public TTransactedRequest
{
    NYTree::TYPath Path;

    TUploadRequest()
    {
        Register("path", Path);
    }
};

typedef TIntrusivePtr<TUploadRequest> TUploadRequestPtr;

class TUploadCommand
    : public TCommandBase<TUploadRequest>
{
public:
    TUploadCommand(ICommandHost* commandHost)
        : TCommandBase(commandHost)
    { }

private:
    virtual void DoExecute(TUploadRequestPtr request);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NDriver
} // namespace NYT

