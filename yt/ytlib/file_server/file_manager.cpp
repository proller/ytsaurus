#include "stdafx.h"
#include "file_manager.h"
#include "file_node_proxy.h"

namespace NYT {
namespace NFileServer {

using namespace NMetaState;
using namespace NCypress;
using namespace NChunkServer;
using namespace NTransaction;
using namespace NProto;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = FileServerLogger;

////////////////////////////////////////////////////////////////////////////////

TFileManagerBase::TFileManagerBase(
    TCypressManager* cypressManager,
    TChunkManager* chunkManager,
    TTransactionManager* transactionManager)
    : CypressManager(cypressManager)
    , ChunkManager(chunkManager)
    , TransactionManager(transactionManager)
{
    YASSERT(cypressManager != NULL);
    YASSERT(chunkManager != NULL);
    YASSERT(transactionManager != NULL);
}

void TFileManagerBase::ValidateTransactionId(
    const TTransactionId& transactionId,
    bool mayBeNull)
{
    if ((transactionId != NullTransactionId || !mayBeNull) &&
        TransactionManager->FindTransaction(transactionId) == NULL)
    {
        ythrow TServiceException(EErrorCode::NoSuchTransaction) << 
            Sprintf("Invalid transaction id (TransactionId: %s)", ~transactionId.ToString());
    }
}

TFileNode& TFileManagerBase::GetFileNode(const TNodeId& nodeId, const TTransactionId& transactionId)
{
    auto* impl = CypressManager->FindTransactionNodeForUpdate(nodeId, transactionId);
    if (impl == NULL) {
        ythrow TServiceException(EErrorCode::NoSuchNode) << 
            Sprintf("Invalid file node id (NodeId: %s, TransactionId: %s)",
                ~nodeId.ToString(),
                ~transactionId.ToString());
    }

    auto* typedImpl = dynamic_cast<TFileNode*>(impl);
    if (typedImpl == NULL) {
        ythrow TServiceException(EErrorCode::NotAFile) << 
            Sprintf("Not a file node (NodeId: %s, TransactionId: %s)",
                ~nodeId.ToString(),
                ~transactionId.ToString());
    }

    return *typedImpl;
}

TChunk& TFileManagerBase::GetChunk(const TChunkId& chunkId)
{
    auto* chunk = ChunkManager->FindChunkForUpdate(chunkId);
    if (chunk == NULL) {
        ythrow TServiceException(EErrorCode::NoSuchChunk) << 
            Sprintf("Invalid chunk id (ChunkId: %s)", ~chunkId.ToString());
    }
    return *chunk;
}

////////////////////////////////////////////////////////////////////////////////

TFileManager::TFileManager(
    TMetaStateManager* metaStateManager,
    TCompositeMetaState* metaState,
    TCypressManager* cypressManager,
    TChunkManager* chunkManager,
    TTransactionManager* transactionManager)
    : TMetaStatePart(metaStateManager, metaState)
    , TFileManagerBase(cypressManager, chunkManager, transactionManager)
{
    RegisterMethod(this, &TThis::SetFileChunk);

    cypressManager->RegisterNodeType(~New<TFileNodeTypeHandler>(
        cypressManager,
        this,
        chunkManager));

    metaState->RegisterPart(this);
}

Stroka TFileManager::GetPartName() const
{
    return "FileManager";
}

TMetaChange<TVoid>::TPtr
TFileManager::InitiateSetFileChunk(
    const TNodeId& nodeId,
    const TTransactionId& transactionId,
    const TChunkId& chunkId)
{
    TMsgSetFileChunk message;
    message.SetTransactionId(transactionId.ToProto());
    message.SetNodeId(nodeId.ToProto());
    message.SetChunkId(chunkId.ToProto());

    return CreateMetaChange(
        MetaStateManager,
        message,
        &TThis::SetFileChunk,
        TPtr(this),
        ECommitMode::MayFail);
}

TVoid TFileManager::SetFileChunk(const NProto::TMsgSetFileChunk& message)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    auto transactionId = TTransactionId::FromProto(message.GetTransactionId());
    auto nodeId = TNodeId::FromProto(message.GetNodeId());
    auto chunkId = TChunkId::FromProto(message.GetChunkId());

    ValidateTransactionId(transactionId, false);

    auto& chunk = GetChunk(chunkId);
    auto& fileNode = GetFileNode(nodeId, transactionId);

    if (fileNode.GetChunkListId() != NullChunkListId) {
        // TODO: exception type
        throw yexception() << "Chunk is already assigned to file node";
    }

    auto& chunkList = ChunkManager->CreateChunkList();
    fileNode.SetChunkListId(chunkList.GetId());
    ChunkManager->RefChunkList(chunkList);

    chunkList.Chunks().push_back(chunkId);
    ChunkManager->RefChunk(chunk);

    return TVoid();
}

TChunkId TFileManager::GetFileChunk(
    const TNodeId& nodeId,
    const TTransactionId& transactionId)
{
    ValidateTransactionId(transactionId, true);
    auto& fileNode = GetFileNode(nodeId, transactionId);

    if (fileNode.GetChunkListId() == NullChunkId) {
        return NullChunkId;
    }

    const auto& chunkList = ChunkManager->GetChunkList(fileNode.GetChunkListId());
    YASSERT(chunkList.Chunks().ysize() == 1);
    return chunkList.Chunks()[0];
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NFileServer
} // namespace NYT
