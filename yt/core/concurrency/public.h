#pragma once

#include <core/misc/common.h>

namespace NYT {
namespace NConcurrency {

////////////////////////////////////////////////////////////////////////////////

typedef size_t TThreadId;
const TThreadId InvalidThreadId = 0;

////////////////////////////////////////////////////////////////////////////////

class TActionQueue;
typedef TIntrusivePtr<TActionQueue> TActionQueuePtr;

class TFairShareActionQueue;
typedef TIntrusivePtr<TFairShareActionQueue> TFairShareActionQueuePtr;

class TPrioritizedActionQueue;
typedef TIntrusivePtr<TPrioritizedActionQueue> TPrioritizedActionQueuePtr;

class TThreadPool;
typedef TIntrusivePtr<TThreadPool> TThreadPoolPtr;

class TParallelAwaiter;
typedef TIntrusivePtr<TParallelAwaiter> TParallelAwaiterPtr;

class TPeriodicExecutor;
typedef TIntrusivePtr<TPeriodicExecutor> TPeriodicExecutorPtr;

class TThroughputThrottlerConfig;
typedef TIntrusivePtr<TThroughputThrottlerConfig> TThroughputThrottlerConfigPtr;

class IThroughputThrottler;
typedef TIntrusivePtr<IThroughputThrottler> IThroughputThrottlerPtr;

class TAsyncSemaphore;

struct IAsyncInputStream;
typedef TIntrusivePtr<IAsyncInputStream> IAsyncInputStreamPtr;

struct IAsyncOutputStream;
typedef TIntrusivePtr<IAsyncOutputStream> IAsyncOutputStreamPtr;

class TFiber;
typedef TIntrusivePtr<TFiber> TFiberPtr;

template <class Signature>
class TCoroutine;

////////////////////////////////////////////////////////////////////////////////

} // namespace NConcurrency
} // namespace NYT
