#pragma once

#include "public.h"
#include "object_proxy.h"

#include <core/misc/nullable.h>

#include <core/ytree/public.h>

#include <core/rpc/service_detail.h>

#include <ytlib/object_client/master_ypath.pb.h>

#include <server/transaction_server/public.h>

#include <server/security_server/acl.h>

namespace NYT {
namespace NObjectServer {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EObjectTransactionMode,
    (Required)
    (Forbidden)
    (Optional)
);

DEFINE_ENUM(EObjectAccountMode,
    (Required)
    (Forbidden)
    (Optional)
);

DEFINE_BIT_ENUM(EObjectReplicationFlags,
    ((None)        (0x0000))
    ((Create)      (0x0001)) // replicate object creation
    ((Destroy)     (0x0002)) // replicate object destruction
    ((Attributes)  (0x0004)) // replicate object attribute changes
    ((All)         (0xffff)) // replicate all possible actions
);

struct TTypeCreationOptions
{
    TTypeCreationOptions() = default;

    TTypeCreationOptions(
        EObjectTransactionMode transactionMode,
        EObjectAccountMode accountMode);

    EObjectTransactionMode TransactionMode = EObjectTransactionMode::Forbidden;
    EObjectAccountMode AccountMode = EObjectAccountMode::Forbidden;
};

// WinAPI is great.
#undef GetObject

//! Provides a bridge between TObjectManager and concrete object implementations.
struct IObjectTypeHandler
    : public virtual TRefCounted
{
    //! Returns a bunch of flags that control object replication.
    virtual EObjectReplicationFlags GetReplicationFlags() const = 0;

    //! Returns the object type managed by the handler.
    virtual EObjectType GetType() const = 0;

    //! Returns a human-readable object name.
    virtual Stroka GetName(TObjectBase* object) = 0;

    //! Finds object by id, returns |nullptr| if nothing is found.
    virtual TObjectBase* FindObject(const TObjectId& id) = 0;

    //! Finds object by id, fails if nothing is found.
    TObjectBase* GetObject(const TObjectId& id);

    //! Given a versioned object id, constructs a proxy for it.
    //! The object with the given id must exist.
    virtual IObjectProxyPtr GetProxy(
        TObjectBase* object,
        NTransactionServer::TTransaction* transaction) = 0;

    //! Returns options used for creating new instances of this type
    //! or |Null| if the type does not support creating new instances.
    //! In the latter case #Create is never called.
    virtual TNullable<TTypeCreationOptions> GetCreationOptions() const = 0;

    typedef NRpc::TTypedServiceRequest<NObjectClient::NProto::TReqCreateObject> TReqCreateObject;
    typedef NRpc::TTypedServiceResponse<NObjectClient::NProto::TRspCreateObject> TRspCreateObject;
    //! Creates a new object instance.
    /*!
     *  \param hintId Id for the new object, if |NullObjectId| then a new id is generated.
     *  \param transaction Transaction that becomes the owner of the newly created object.
     *  \param request Creation request (possibly containing additional parameters).
     *  \param response Creation response (which may also hold some additional result).
     *  \returns the newly created object.
     *
     *  Once #Create is completed, all request attributes are copied to object attributes.
     *  The handler may alter the request appropriately to control this process.
     */
    virtual TObjectBase* CreateObject(
        const TObjectId& hintId,
        NTransactionServer::TTransaction* transaction,
        NSecurityServer::TAccount* account,
        NYTree::IAttributeDictionary* attributes,
        TReqCreateObject* request,
        TRspCreateObject* response) = 0;

    //! Raised when the strong ref-counter of the object decreases to zero.
    virtual void ZombifyObject(TObjectBase* object) throw() = 0;

    //! Raised when GC finally destroys the object.
    virtual void DestroyObject(TObjectBase* object) throw() = 0;

    //! Given #object, returns its staging transaction or |nullptr| is #object
    //! is not staged.
    virtual NTransactionServer::TTransaction* GetStagingTransaction(
        TObjectBase* object) = 0;

    //! Resets staging information for #object.
    /*!
     *  If #recursive is |true| then all child objects are also released.
     */
    virtual void UnstageObject(TObjectBase* object, bool recursive) = 0;

    //! Returns the object ACD or |nullptr| if access is not controlled.
    virtual NSecurityServer::TAccessControlDescriptor* FindAcd(TObjectBase* object) = 0;

    //! Returns the object containing parent ACL.
    virtual TObjectBase* GetParent(TObjectBase* object) = 0;

    //! Returns the set of all permissions supported by this object type.
    virtual NYTree::EPermissionSet GetSupportedPermissions() const = 0;

    //! Resets the transient state of all managed objects.
    /*!
     *  This is called upon recovery startup.
     *  Among other things, the handler must reset weak ref counters to zero.
     */
    virtual void ResetAllObjects() = 0;

};

DEFINE_REFCOUNTED_TYPE(IObjectTypeHandler)

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT

