#pragma once

#include "public.h"

#include <yt/yt/ytlib/chunk_client/public.h>

#include <yt/yt/client/table_client/schema.h>

#include <yt/yt/client/security_client/public.h>

#include <yt/yt/library/erasure/public.h>

#include <yt/yt/core/compression/public.h>

#include <yt/yt/core/misc/phoenix.h>

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

struct TTableUploadOptions
{
    NChunkClient::EUpdateMode UpdateMode;
    NCypressClient::ELockMode LockMode;
    TTableSchemaPtr TableSchema = New<TTableSchema>();
    ETableSchemaModification SchemaModification;
    ETableSchemaMode SchemaMode;
    EOptimizeFor OptimizeFor;
    NCompression::ECodec CompressionCodec;
    NErasure::ECodec ErasureCodec;
    std::optional<std::vector<NSecurityClient::TSecurityTag>> SecurityTags;
    bool PartiallySorted;

    TTableSchemaPtr GetUploadSchema() const;

    void Persist(const NPhoenix::TPersistenceContext& context);
};

TTableUploadOptions GetTableUploadOptions(
    const NYPath::TRichYPath& path,
    const NYTree::IAttributeDictionary& cypressTableAttributes,
    i64 rowCount);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
