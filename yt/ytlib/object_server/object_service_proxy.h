#pragma once

#include "id.h"
#include <ytlib/object_server/object_service.pb.h>

#include <ytlib/ytree/ypath_client.h>

namespace NYT {
namespace NObjectServer {

////////////////////////////////////////////////////////////////////////////////

class TObjectServiceProxy
    : public NRpc::TProxyBase
{
public:
    typedef TIntrusivePtr<TObjectServiceProxy> TPtr;

    static Stroka GetServiceName()
    {
        return "ObjectService";
    }

    TObjectServiceProxy(NRpc::IChannelPtr channel)
        : TProxyBase(channel, GetServiceName())
    { }

    DEFINE_RPC_PROXY_METHOD(NProto, Execute);

    //! Executes a single Cypress request.
    template <class TTypedRequest>
    TFuture< TIntrusivePtr<typename TTypedRequest::TTypedResponse> >
    Execute(TIntrusivePtr<TTypedRequest> innerRequest);

    class TReqExecuteBatch;
    class TRspExecuteBatch;

    //! A batched request to Cypress that holds a vector of individual requests that
    //! are transferred within a single RPC envelope.
    class TReqExecuteBatch
        : public NRpc::TClientRequest
    {
    public:
        typedef TIntrusivePtr<TReqExecuteBatch> TPtr;

        TReqExecuteBatch(
            NRpc::IChannelPtr channel,
            const Stroka& path,
            const Stroka& verb);

        TFuture< TIntrusivePtr<TRspExecuteBatch> > Invoke();

        // Overrides base method for fluent use.
        TIntrusivePtr<TReqExecuteBatch> SetTimeout(TNullable<TDuration> timeout)
        {
            TClientRequest::SetTimeout(timeout);
            return this;
        }

        //! Adds an individual request into the batch.
        /*!
         *  Each individual request may be marked with a key.
         *  These keys can be used to retrieve the corresponding responses
         *  (thus avoiding complicated and error-prone index calculations).
         */
        TIntrusivePtr<TReqExecuteBatch> AddRequest(
            NYTree::TYPathRequestPtr innerRequest,
            const Stroka& key = "");

        //! Returns the current number of individual requests in the batch.
        int GetSize() const;

    private:
        typedef std::multimap<Stroka, int> TKeyToIndexes;

        NProto::TRspExecute Body;
        TKeyToIndexes KeyToIndexes;

        virtual TBlob SerializeBody() const;

    };

    //! A response to a batched request.
    /*!
     *  This class holds a vector of messages representing responses to individual
     *  requests that were earlier sent to Cypress.
     *  
     *  The length of this vector (see #GetSize) coincides to that of the requests vector.
     *  
     *  Individual responses can be extracted by calling #GetResponse. Since they may be of
     *  different actual types, the client must supply an additional type parameter.
     *  Responses may also be retrieved by specifying a key that was used during
     *  request insertion.
     *  
     */
    class TRspExecuteBatch
        : public NRpc::TClientResponse
    {
    public:
        typedef TIntrusivePtr<TRspExecuteBatch> TPtr;
        typedef std::multimap<Stroka, int> TKeyToIndexMultimap;

        TRspExecuteBatch(
            const NRpc::TRequestId& requestId,
            const TKeyToIndexMultimap& keyToIndexes);

        TFuture<TPtr> GetAsyncResult();

        //! Returns the number of individual responses in the batch.
        int GetSize() const;

        //! Returns the individual response with a given index.
        template <class TTypedResponse>
        TIntrusivePtr<TTypedResponse> GetResponse(int index) const;

        //! Returns the individual generic response with a given index.
        NYTree::TYPathResponse::TPtr GetResponse(int index) const;

        //! Returns the individual generic response with a given key.
        //! Such a response must be unique.
        NYTree::TYPathResponse::TPtr GetResponse(const Stroka& key) const;

        //! Returns the individual response with a given key.
        //! Such a response must be unique.
        template <class TTypedResponse>
        TIntrusivePtr<TTypedResponse> GetResponse(const Stroka& key) const;

        //! Returns all responses with a given key (all if no key is specified).
        template <class TTypedResponse>
        std::vector< TIntrusivePtr<TTypedResponse> > GetResponses(const Stroka& key = "") const;

        //! Returns all responses with a given key (all if no key is specified).
        std::vector<NYTree::TYPathResponse::TPtr> GetResponses(const Stroka& key = "") const;

    private:
        TKeyToIndexMultimap KeyToIndexes;
        TPromise<TPtr> Promise;
        NProto::TRspExecute Body;
        yvector<int> BeginPartIndexes;

        virtual void FireCompleted();
        virtual void DeserializeBody(const TRef& data);

    };

    // TODO(panin): think about introducing macros
    //! Mimics the types introduced by |DEFINE_RPC_PROXY_METHOD|.
    typedef TIntrusivePtr<TRspExecuteBatch> TRspExecuteBatchPtr;
    typedef TIntrusivePtr<TReqExecuteBatch> TReqExecuteBatchPtr;
    typedef TFuture<TRspExecuteBatchPtr> TInvExecuteBatch;

    //! Executes a batched Cypress request.
    TReqExecuteBatchPtr ExecuteBatch();

};

////////////////////////////////////////////////////////////////////////////////


} // namespace NObjectServer
} // namespace NYT

#define OBJECT_SERVICE_PROXY_INL_H_
#include "object_service_proxy-inl.h"
#undef OBJECT_SERVICE_PROXY_INL_H_
