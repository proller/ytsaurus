#include "stdafx.h"
#include "journal_node_proxy.h"
#include "journal_node.h"
#include "private.h"

#include <server/chunk_server/chunk_owner_node_proxy.h>
#include <server/chunk_server/chunk_manager.h>
#include <server/chunk_server/chunk.h>
#include <server/chunk_server/chunk_list.h>

namespace NYT {
namespace NJournalServer {

using namespace NChunkClient;
using namespace NChunkServer;
using namespace NCypressServer;
using namespace NYTree;
using namespace NYson;
using namespace NTransactionServer;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

class TJournalNodeProxy
    : public TCypressNodeProxyBase<TChunkOwnerNodeProxy, IEntityNode, TJournalNode>
{
public:
    TJournalNodeProxy(
        INodeTypeHandlerPtr typeHandler,
        TBootstrap* bootstrap,
        TTransaction* transaction,
        TJournalNode* trunkNode)
        : TBase(
            typeHandler,
            bootstrap,
            transaction,
            trunkNode)
    { }

private:
    typedef TCypressNodeProxyBase<TChunkOwnerNodeProxy, IEntityNode, TJournalNode> TBase;

    virtual NLog::TLogger CreateLogger() const override
    {
        return JournalServerLogger;
    }

    virtual ELockMode GetLockMode(EUpdateMode updateMode) override
    {
        return ELockMode::Exclusive;
    }

    virtual void ListSystemAttributes(std::vector<TAttributeInfo>* attributes) override
    {
        attributes->push_back("read_quorum");
        attributes->push_back("write_quorum");
        attributes->push_back("sealed");
        attributes->push_back("record_count");
        attributes->push_back(TAttributeInfo("quorum_record_count", true, true));
        TBase::ListSystemAttributes(attributes);
    }

    virtual bool GetBuiltinAttribute(const Stroka& key, IYsonConsumer* consumer) override
    {
        const auto* node = GetThisTypedImpl();

        if (key == "read_quorum") {
            BuildYsonFluently(consumer)
                .Value(node->GetReadQuorum());
            return true;
        }

        if (key == "write_quorum") {
            BuildYsonFluently(consumer)
                .Value(node->GetWriteQuorum());
            return true;
        }

        if (key == "sealed") {
            BuildYsonFluently(consumer)
                .Value(node->IsSealed());
            return true;
        }

        if (key == "record_count") {
            BuildYsonFluently(consumer)
                .Value(node->GetChunkList()->Statistics().RecordCount);
            return true;
        }

        return TBase::GetBuiltinAttribute(key, consumer);
    }

    virtual bool SetBuiltinAttribute(const Stroka& key, const TYsonString& value) override
    {
        if (key == "replication_factor") {
            // Prevent changing replication factor after construction.
            ValidateNoTransaction();
            auto* node = GetThisTypedImpl();
            YCHECK(node->IsTrunk());
            if (node->GetReplicationFactor() != 0) {
                ThrowCannotSetBuiltinAttribute("replication_factor");
            } else {
                return TCypressNodeProxyBase::SetBuiltinAttribute(key, value);
            }
        }

        if (key == "read_quorum") {
            int readQuorum = NYTree::ConvertTo<int>(value);
            if (readQuorum < 1) {
                THROW_ERROR_EXCEPTION("Value must be positive");
            }

            ValidateNoTransaction();
            auto* node = GetThisTypedImpl();
            YCHECK(node->IsTrunk());

            // Prevent changing read quorum after construction.
            if (node->GetReadQuorum() != 0) {
                ThrowCannotSetBuiltinAttribute("read_quorum");
            }
            node->SetReadQuorum(readQuorum);
            return true;
        }

        if (key == "write_quorum") {
            int writeQuorum = NYTree::ConvertTo<int>(value);
            if (writeQuorum < 1) {
                THROW_ERROR_EXCEPTION("Value must be positive");
            }

            ValidateNoTransaction();
            auto* node = GetThisTypedImpl();
            YCHECK(node->IsTrunk());

            // Prevent changing write quorum after construction.
            if (node->GetWriteQuorum() != 0) {
                ThrowCannotSetBuiltinAttribute("write_quorum");
            }
            node->SetWriteQuorum(writeQuorum);
            return true;
        }

        return TBase::SetBuiltinAttribute(key, value);
    }

    virtual TAsyncError GetBuiltinAttributeAsync(const Stroka& key, IYsonConsumer* consumer) override
    {
        const auto* node = GetThisTypedImpl();
        if (key == "quorum_record_count") {
            const auto* chunkList = node->GetChunkList();
            if (chunkList->Children().empty()) {
                BuildYsonFluently(consumer)
                    .Value(0);
                return OKFuture;
            }

            auto* chunk = chunkList->Children().back()->AsChunk();
            i64 penultimateRecordCount = chunkList->RecordCountSums().empty() ? 0 : chunkList->RecordCountSums().back();

            auto chunkManager = Bootstrap->GetChunkManager();
            auto recordCountResult = chunkManager->GetChunkQuorumRecordCount(chunk);

            return recordCountResult.Apply(BIND([=] (TErrorOr<int> recordCountOrError) -> TError {
                if (recordCountOrError.IsOK()) {
                    BuildYsonFluently(consumer)
                        .Value(penultimateRecordCount + recordCountOrError.Value());
                }
                return TError(recordCountOrError);
            }));
        }

        return TBase::GetBuiltinAttributeAsync(key, consumer);
    }


    virtual bool DoInvoke(NRpc::IServiceContextPtr context) override
    {
        DISPATCH_YPATH_SERVICE_METHOD(PrepareForUpdate);
        return TBase::DoInvoke(context);
    }

    DECLARE_YPATH_SERVICE_METHOD(NChunkClient::NProto, PrepareForUpdate)
    {
        DeclareMutating();

        auto mode = EUpdateMode(request->mode());
        if (mode != EUpdateMode::Append) {
            THROW_ERROR_EXCEPTION("Journals only support %s update mode",
                ~FormatEnum(EUpdateMode(EUpdateMode::Append)).Quote());
        }

        ValidateTransaction();
        ValidatePermission(
            NYTree::EPermissionCheckScope::This,
            NSecurityServer::EPermission::Write);

        auto* node = GetThisTypedImpl();
        if (!node->IsSealed()) {
            THROW_ERROR_EXCEPTION("Journal is not properly sealed");
        }

        ValidatePrepareForUpdate();

        auto* lockedNode = LockThisTypedImpl();
        auto* chunkList = node->GetChunkList();

        lockedNode->SetUpdateMode(mode);

        SetModified();

        LOG_DEBUG_UNLESS(
            IsRecovery(),
            "Node is switched to \"append\" mode (NodeId: %s, ChunkListId: %s)",
            ~ToString(node->GetId()),
            ~ToString(chunkList->GetId()));

        ToProto(response->mutable_chunk_list_id(), chunkList->GetId());

        context->SetResponseInfo("ChunkListId: %s",
            ~ToString(chunkList->GetId()));

        context->Reply();
    }
};

////////////////////////////////////////////////////////////////////////////////

ICypressNodeProxyPtr CreateJournalNodeProxy(
    INodeTypeHandlerPtr typeHandler,
    TBootstrap* bootstrap,
    TTransaction* transaction,
    TJournalNode* trunkNode)
{

    return New<TJournalNodeProxy>(
        typeHandler,
        bootstrap,
        transaction,
        trunkNode);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NJournalServer
} // namespace NYT
