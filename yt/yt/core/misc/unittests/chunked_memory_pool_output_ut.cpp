#include <yt/yt/core/test_framework/framework.h>

#include <yt/yt/core/misc/chunked_memory_pool.h>
#include <yt/yt/core/misc/chunked_memory_pool_output.h>

namespace NYT {
namespace {

////////////////////////////////////////////////////////////////////////////////

TEST(TChunkedMemoryPoolOutputTest, Basic)
{
    constexpr size_t PoolChunkSize = 10;
    constexpr size_t PoolOutputChunkSize = 7;
    TChunkedMemoryPool pool(NullRefCountedTypeCookie, PoolChunkSize);
    TChunkedMemoryPoolOutput output(&pool, PoolOutputChunkSize);

    TString s1("Short.");
    output.Write(s1);

    TString s2("Quite a long string.");
    output.Write(s2);

    char* buf;
    auto len = output.Next(&buf);
    output.Undo(len);

    auto chunks = output.FinishAndGetRefs();
    TString s;
    for (auto chunk : chunks) {
        s += TString(chunk.Begin(), chunk.End());
    }
    ASSERT_EQ(s1 + s2, s);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT
