#pragma once

#include "public.h"

#include <ytlib/rpc/service_detail.h>

#include <ytlib/ytree/public.h>

#include <ytlib/cypress_client/cypress_ypath.pb.h>

#include <server/transaction_server/public.h>

#include <server/security_server/public.h>

namespace NYT {
namespace NCypressServer {

////////////////////////////////////////////////////////////////////////////////

//! Describes a behavior object that lives as long as the node
//! exists in Cypress.
/*!
 *  \note
 *  Behaviors are only created at leaders.
 *  Behaviors are only created for trunk nodes.
 */
struct INodeBehavior
    : public virtual TRefCounted
{
    //! Called when the node owning the behavior object is about to
    //! be destroyed.
    virtual void Destroy() = 0;
};

////////////////////////////////////////////////////////////////////////////////

//! Provides node type-specific services.
struct INodeTypeHandler
    : public virtual TRefCounted
{
    //! Constructs a proxy.
    /*!
     *  \param transactionId The id of the transaction for which the proxy
     *  is being created (possibly #NullTransactionId).
     *  \return The constructed proxy.
     */
    virtual ICypressNodeProxyPtr GetProxy(
        TCypressNodeBase* trunkNode,
        NTransactionServer::TTransaction* transaction) = 0;

    //! Returns the (dynamic) node type.
    virtual NObjectClient::EObjectType GetObjectType() = 0;

    //! Returns the (static) node type.
    virtual NYTree::ENodeType GetNodeType() = 0;

    //! Create an empty instance of the node (used during snapshot deserialization).
    virtual std::unique_ptr<TCypressNodeBase> Instantiate(const TVersionedNodeId& id) = 0;

    typedef NRpc::TTypedServiceRequest<NCypressClient::NProto::TReqCreate> TReqCreate;
    typedef NRpc::TTypedServiceResponse<NCypressClient::NProto::TRspCreate> TRspCreate;
    //! Creates and registers a node.
    /*!
     *  This is called during |Create|.
     */
    virtual std::unique_ptr<TCypressNodeBase> Create(
        NTransactionServer::TTransaction* transaction,
        TReqCreate* request,
        TRspCreate* response) = 0;

    //! Called during node creation to populate default attributes that are missing.
    virtual void SetDefaultAttributes(NYTree::IAttributeDictionary* attributes) = 0;

    //! Performs cleanup on node destruction.
    /*!
     *  This is called prior to the actual removal of the node from the meta-map.
     *  A typical implementation will release the resources held by the node,
     *  decrement the ref-counters of its children etc.
     */
    virtual void Destroy(TCypressNodeBase* node) = 0;

    //! Branches a node into a given transaction.
    /*!
     *  \param node The originating node.
     *  \param transaction Transaction that needs a copy of the node.
     *  \param mode The lock mode for which the node is being branched.
     *  \returns The branched node.
     */
    virtual std::unique_ptr<TCypressNodeBase> Branch(
        TCypressNodeBase* originatingNode,
        NTransactionServer::TTransaction* transaction,
        ELockMode mode) = 0;

    //! Merges the changes made in the branched node back into the committed one.
    /*!
     *  \param branchedNode The branched node.
     *
     *  \note
     *  #branchedNode is non-const for performance reasons (i.e. to swap the data instead of copying).
     */
    virtual void Merge(
        TCypressNodeBase* originatingNode,
        TCypressNodeBase* branchedNode) = 0;

    //! Constructs a deep copy of the node.
    virtual std::unique_ptr<TCypressNodeBase> Clone(
        TCypressNodeBase* sourceNode,
        const TCloneContext& context) = 0;

    //! Creates a behavior associated with the node.
    //! The method may return NULL if no behavior is needed.
    virtual INodeBehaviorPtr CreateBehavior(TCypressNodeBase* trunkNode) = 0;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypressServer
} // namespace NYT
