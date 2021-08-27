#include "master_type_handler.h"
#include "type_handler_detail.h"
#include "master.h"
#include "master_proxy.h"

namespace NYT::NObjectServer {

using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

class TMasterTypeHandler
    : public TObjectTypeHandlerBase<TMasterObject>
{
public:
    explicit TMasterTypeHandler(TBootstrap* bootstrap)
        : TObjectTypeHandlerBase(bootstrap)
    { }

    EObjectType GetType() const override
    {
        return EObjectType::Master;
    }

    TObject* FindObject(TObjectId id) override
    {
        const auto& objectManager = Bootstrap_->GetObjectManager();
        auto* object = objectManager->GetMasterObject();
        return id == object->GetId() ? object : nullptr;
    }

private:
    void DoDestroyObject(TMasterObject* /*object*/) noexcept override
    {
        YT_ABORT();
    }

    IObjectProxyPtr DoGetProxy(
        TMasterObject* object,
        NTransactionServer::TTransaction* /*transaction*/) override
    {
        return CreateMasterProxy(Bootstrap_, &Metadata_, object);
    }
};

IObjectTypeHandlerPtr CreateMasterTypeHandler(TBootstrap* bootstrap)
{
    return New<TMasterTypeHandler>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NObjectServer
