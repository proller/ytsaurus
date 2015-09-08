#pragma once

#include "public.h"
#include "chunk_owner_base.h"

#include <ytlib/chunk_client/chunk_owner_ypath_proxy.h>
#include <ytlib/chunk_client/schema.h>
#include <ytlib/chunk_client/read_limit.h>

#include <server/cypress_server/node_proxy_detail.h>

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

class TChunkOwnerNodeProxy
    : public NCypressServer::TNontemplateCypressNodeProxyBase
{
public:
    TChunkOwnerNodeProxy(
        NCypressServer::INodeTypeHandlerPtr typeHandler,
        NCellMaster::TBootstrap* bootstrap,
        NTransactionServer::TTransaction* transaction,
        TChunkOwnerBase* trunkNode);

protected:
    virtual void ListSystemAttributes(std::vector<NYTree::ISystemAttributeProvider::TAttributeDescriptor>* descriptors) override;
    virtual bool GetBuiltinAttribute(const Stroka& key, NYson::IYsonConsumer* consumer) override;
    virtual TFuture<NYson::TYsonString> GetBuiltinAttributeAsync(const Stroka& key) override;
    virtual void ValidateCustomAttributeUpdate(
        const Stroka& key,
        const TNullable<NYson::TYsonString>& oldValue,
        const TNullable<NYson::TYsonString>& newValue) override;
    virtual void ValidateFetchParameters(
        const NChunkClient::TChannel& channel,
        const std::vector<NChunkClient::TReadRange>& ranges);

    virtual bool SetBuiltinAttribute(const Stroka& key, const NYson::TYsonString& value) override;

    virtual bool DoInvoke(NRpc::IServiceContextPtr context) override;

    void ValidateInUpdate();
    virtual void ValidateBeginUpload();
    virtual void ValidateFetch();

    DECLARE_YPATH_SERVICE_METHOD(NChunkClient::NProto, Fetch);
    DECLARE_YPATH_SERVICE_METHOD(NChunkClient::NProto, BeginUpload);
    DECLARE_YPATH_SERVICE_METHOD(NChunkClient::NProto, GetUploadParams);
    DECLARE_YPATH_SERVICE_METHOD(NChunkClient::NProto, EndUpload);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
