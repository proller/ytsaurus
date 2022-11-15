#pragma once

#include "serialize.h"

#include <yt/yt/client/table_client/serialize.h>

namespace NYT::NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

class TLoadContext
    : public NTableClient::TLoadContext
{
public:
    using NTableClient::TLoadContext::TLoadContext;

    ESnapshotVersion GetVersion() const;
};

////////////////////////////////////////////////////////////////////////////////

class TSaveContext
    : public NTableClient::TSaveContext
{
public:
    explicit TSaveContext(IZeroCopyOutput* output);

    ESnapshotVersion GetVersion() const;
};

////////////////////////////////////////////////////////////////////////////////

using TPersistenceContext = TCustomPersistenceContext<TSaveContext, TLoadContext, ESnapshotVersion>;
using IPersistent = NPhoenix::ICustomPersistent<TPersistenceContext>;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
