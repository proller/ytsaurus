#pragma once

#include "common.h"
#include "ref.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class TChunkedMemoryPool
{
public:
    TChunkedMemoryPool(
        size_t chunkSize = 64 * 1024, 
        size_t maxSmallBlockSize = 16 * 1024);

    char* Allocate(size_t size);

    template <class T>
    T* Allocate()
    {
        char* buffer = Allocate(sizeof (T));
        new (buffer) T();
        return reinterpret_cast<T*>(buffer);
    }

    // TODO(babenko): AllocateUnaligned

    void Clear();

private:
    const size_t ChunkSize;
    const size_t MaxSmallBlockSize;

    int ChunkIndex;
    size_t Offset;

    std::vector<TSharedRef> Chunks;
    std::vector<TSharedRef> LargeBlocks;

    void AllocateNewChunk();
    TSharedRef AllocateBlock(size_t size);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
