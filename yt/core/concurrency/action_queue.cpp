#include "action_queue.h"
#include "single_queue_scheduler_thread.h"
#include "fair_share_queue_scheduler_thread.h"
#include "private.h"
#include "profiler_utils.h"

#include <yt/core/actions/invoker_detail.h>

#include <yt/core/profiling/profile_manager.h>

#include <yt/core/ypath/token.h>

namespace NYT {
namespace NConcurrency {

using namespace NProfiling;
using namespace NYPath;
using namespace NYTree;

///////////////////////////////////////////////////////////////////////////////

class TActionQueue::TImpl
    : public TRefCounted
{
public:
    explicit TImpl(
        const Stroka& threadName,
        bool enableLogging,
        bool enableProfiling)
        : Queue_(New<TInvokerQueue>(
            CallbackEventCount_,
            GetThreadTagIds(enableProfiling, threadName),
            enableLogging,
            enableProfiling))
        , Thread_(New<TSingleQueueSchedulerThread>(
            Queue_,
            CallbackEventCount_,
            threadName,
            GetThreadTagIds(enableProfiling, threadName),
            enableLogging,
            enableProfiling))
    { }

    ~TImpl()
    {
        Shutdown();
    }

    void Start()
    {
        Thread_->Start();
        // XXX(sandello): Racy! Fix me by moving this into OnThreadStart().
        Queue_->SetThreadId(Thread_->GetId());
    }

    void Shutdown()
    {
        Queue_->Shutdown();

        GetFinalizerInvoker()->Invoke(BIND([thread = Thread_] {
            thread->Shutdown();
        }));
    }

    bool IsStarted() const
    {
        return Thread_->IsStarted();
    }

    IInvokerPtr GetInvoker()
    {
        if (Y_UNLIKELY(!IsStarted())) {
            Start();
        }
        return Queue_;
    }

private:
    const std::shared_ptr<TEventCount> CallbackEventCount_ = std::make_shared<TEventCount>();
    const TInvokerQueuePtr Queue_;
    const TSingleQueueSchedulerThreadPtr Thread_;
};

TActionQueue::TActionQueue(
    const Stroka& threadName,
    bool enableLogging,
    bool enableProfiling)
    : Impl_(New<TImpl>(threadName, enableLogging, enableProfiling))
{ }

TActionQueue::~TActionQueue() = default;

void TActionQueue::Shutdown()
{
    return Impl_->Shutdown();
}

IInvokerPtr TActionQueue::GetInvoker()
{
    return Impl_->GetInvoker();
}

///////////////////////////////////////////////////////////////////////////////

class TSerializedInvoker
    : public TInvokerWrapper
{
public:
    explicit TSerializedInvoker(IInvokerPtr underlyingInvoker)
        : TInvokerWrapper(std::move(underlyingInvoker))
    {
        Lock_.clear();
    }

    virtual void Invoke(const TClosure& callback) override
    {
        Queue_.Enqueue(callback);
        TrySchedule();
    }

private:
    TLockFreeQueue<TClosure> Queue_;
    std::atomic_flag Lock_;


    class TInvocationGuard
    {
    public:
        explicit TInvocationGuard(TIntrusivePtr<TSerializedInvoker> owner)
            : Owner_(std::move(owner))
        { }

        TInvocationGuard(TInvocationGuard&& other) = default;
        TInvocationGuard(const TInvocationGuard& other) = delete;

        void Activate()
        {
            YASSERT(!Activated_);
            Activated_ = true;
        }

        void Reset()
        {
            Owner_.Reset();
        }

        ~TInvocationGuard()
        {
            if (Owner_) {
                Owner_->OnFinished(Activated_);
            }
        }

    private:
        TIntrusivePtr<TSerializedInvoker> Owner_;
        bool Activated_ = false;

    };

    void TrySchedule()
    {
        if (Queue_.IsEmpty()) {
            return;
        }

        if (!Lock_.test_and_set(std::memory_order_acquire)) {
            UnderlyingInvoker_->Invoke(BIND(
                &TSerializedInvoker::RunCallback,
                MakeStrong(this),
                Passed(TInvocationGuard(this))));
        }
    }

    void RunCallback(TInvocationGuard invocationGuard)
    {
        invocationGuard.Activate();

        TCurrentInvokerGuard currentInvokerGuard(this);
        TContextSwitchedGuard contextSwitchGuard(BIND(
            &TSerializedInvoker::OnContextSwitched,
            MakeStrong(this),
            &invocationGuard));

        TClosure callback;
        if (Queue_.Dequeue(&callback)) {
            callback.Run();
        }
    }

    void OnContextSwitched(TInvocationGuard* invocationGuard)
    {
        invocationGuard->Reset();
        OnFinished(true);
    }

    void OnFinished(bool scheduleMore)
    {
        Lock_.clear(std::memory_order_release);
        if (scheduleMore) {
            TrySchedule();
        }
    }

};

IInvokerPtr CreateSerializedInvoker(IInvokerPtr underlyingInvoker)
{
    return New<TSerializedInvoker>(underlyingInvoker);
}

///////////////////////////////////////////////////////////////////////////////

class TPrioritizedInvoker
    : public TInvokerWrapper
    , public virtual IPrioritizedInvoker
{
public:
    explicit TPrioritizedInvoker(IInvokerPtr underlyingInvoker)
        : TInvokerWrapper(std::move(underlyingInvoker))
    { }

    using TInvokerWrapper::Invoke;

    virtual void Invoke(const TClosure& callback, i64 priority) override
    {
        {
            TGuard<TSpinLock> guard(SpinLock_);
            TEntry entry;
            entry.Callback = callback;
            entry.Priority = priority;
            Heap_.emplace_back(std::move(entry));
            std::push_heap(Heap_.begin(), Heap_.end());
        }
        UnderlyingInvoker_->Invoke(BIND(&TPrioritizedInvoker::DoExecute, MakeStrong(this)));
    }

private:
    struct TEntry
    {
        TClosure Callback;
        i64 Priority;

        bool operator < (const TEntry& other) const
        {
            return Priority < other.Priority;
        }
    };

    TSpinLock SpinLock_;
    std::vector<TEntry> Heap_;

    void DoExecute()
    {
        TGuard<TSpinLock> guard(SpinLock_);
        std::pop_heap(Heap_.begin(), Heap_.end());
        auto callback = std::move(Heap_.back().Callback);
        Heap_.pop_back();
        guard.Release();
        callback.Run();
    }

};

IPrioritizedInvokerPtr CreatePrioritizedInvoker(IInvokerPtr underlyingInvoker)
{
    return New<TPrioritizedInvoker>(std::move(underlyingInvoker));
}

///////////////////////////////////////////////////////////////////////////////

class TFakePrioritizedInvoker
    : public TInvokerWrapper
    , public virtual IPrioritizedInvoker
{
public:
    explicit TFakePrioritizedInvoker(IInvokerPtr underlyingInvoker)
        : TInvokerWrapper(std::move(underlyingInvoker))
    { }

    using TInvokerWrapper::Invoke;

    virtual void Invoke(const TClosure& callback, i64 /*priority*/) override
    {
        return UnderlyingInvoker_->Invoke(callback);
    }
};

IPrioritizedInvokerPtr CreateFakePrioritizedInvoker(IInvokerPtr underlyingInvoker)
{
    return New<TFakePrioritizedInvoker>(std::move(underlyingInvoker));
}

///////////////////////////////////////////////////////////////////////////////

class TFixedPriorityInvoker
    : public TInvokerWrapper
{
public:
    TFixedPriorityInvoker(
        IPrioritizedInvokerPtr underlyingInvoker,
        i64 priority)
        : TInvokerWrapper(underlyingInvoker)
        , UnderlyingInvoker_(std::move(underlyingInvoker))
        , Priority_(priority)
    { }

    using TInvokerWrapper::Invoke;

    virtual void Invoke(const TClosure& callback) override
    {
        return UnderlyingInvoker_->Invoke(callback, Priority_);
    }

private:
    const IPrioritizedInvokerPtr UnderlyingInvoker_;
    const i64 Priority_;

};

IInvokerPtr CreateFixedPriorityInvoker(
    IPrioritizedInvokerPtr underlyingInvoker,
    i64 priority)
{
    return New<TFixedPriorityInvoker>(
        std::move(underlyingInvoker),
        priority);
}

///////////////////////////////////////////////////////////////////////////////

class TBoundedConcurrencyInvoker
    : public TInvokerWrapper
{
public:
    TBoundedConcurrencyInvoker(
        IInvokerPtr underlyingInvoker,
        int maxConcurrentInvocations,
        const NProfiling::TTagIdList& tagIds)
        : TInvokerWrapper(std::move(underlyingInvoker))
        , MaxConcurrentInvocations_(maxConcurrentInvocations)
        , Semaphore_(0)
        , Profiler("/bounded_concurrency_invoker")
        , SemaphoreCounter_("/semaphore", tagIds)
    { }

    virtual void Invoke(const TClosure& callback) override
    {
        Queue_.Enqueue(callback);
        ScheduleMore();
    }

private:
    int MaxConcurrentInvocations_;

    std::atomic<int> Semaphore_;
    TLockFreeQueue<TClosure> Queue_;

    static PER_THREAD TBoundedConcurrencyInvoker* CurrentSchedulingInvoker_;

    NProfiling::TProfiler Profiler;
    NProfiling::TSimpleCounter SemaphoreCounter_;

    class TInvocationGuard
    {
    public:
        explicit TInvocationGuard(TIntrusivePtr<TBoundedConcurrencyInvoker> owner)
            : Owner_(std::move(owner))
        { }

        TInvocationGuard(TInvocationGuard&& other) = default;
        TInvocationGuard(const TInvocationGuard& other) = delete;

        ~TInvocationGuard()
        {
            if (Owner_) {
                Owner_->OnFinished();
            }
        }

    private:
        TIntrusivePtr<TBoundedConcurrencyInvoker> Owner_;

    };


    void RunCallback(TClosure callback, TInvocationGuard /*invocationGuard*/)
    {
        TCurrentInvokerGuard guard(UnderlyingInvoker_); // sic!
        callback.Run();
    }

    void OnFinished()
    {
        ReleaseSemaphore();
        ScheduleMore();
    }

    void ScheduleMore()
    {
        // Prevent reenterant invocations.
        if (CurrentSchedulingInvoker_ == this)
            return;

        while (true) {
            if (!TryAcquireSemaphore())
                break;

            TClosure callback;
            if (!Queue_.Dequeue(&callback)) {
                ReleaseSemaphore();
                break;
            }

            // If UnderlyingInvoker_ is already terminated, Invoke may drop the guard right away.
            // Protect by setting CurrentSchedulingInvoker_ and checking it on entering ScheduleMore.
            CurrentSchedulingInvoker_ = this;

            UnderlyingInvoker_->Invoke(BIND(
                &TBoundedConcurrencyInvoker::RunCallback,
                MakeStrong(this),
                Passed(std::move(callback)),
                Passed(TInvocationGuard(this))));

            // Don't leave a dangling pointer behind.
            CurrentSchedulingInvoker_ = nullptr;
        }        
    }

    bool TryAcquireSemaphore()
    {
        if (++Semaphore_ <= MaxConcurrentInvocations_) {
            Profiler.Increment(SemaphoreCounter_, 1);
            return true;
        } else {
            --Semaphore_;
            return false;
        }
    }

    void ReleaseSemaphore()
    {
        YCHECK(--Semaphore_ >= 0);
        Profiler.Increment(SemaphoreCounter_, -1);
    }
};

PER_THREAD TBoundedConcurrencyInvoker* TBoundedConcurrencyInvoker::CurrentSchedulingInvoker_ = nullptr;

IInvokerPtr CreateBoundedConcurrencyInvoker(
    IInvokerPtr underlyingInvoker,
    int maxConcurrentInvocations,
    const Stroka& invokerName)
{
    return New<TBoundedConcurrencyInvoker>(
        underlyingInvoker,
        maxConcurrentInvocations,
        GetInvokerTagIds(invokerName));
}

///////////////////////////////////////////////////////////////////////////////

class TSuspendableInvoker
    : public TInvokerWrapper
    , public virtual ISuspendableInvoker
{
public:
    explicit TSuspendableInvoker(IInvokerPtr underlyingInvoker)
        : TInvokerWrapper(std::move(underlyingInvoker))
    { }

    virtual void Invoke(const TClosure& callback) override
    {
        Queue_.Enqueue(callback);
        if (!Suspended_) {
            ScheduleMore();
        }
    }

    TFuture<void> Suspend() override
    {
        if (!Suspended_.exchange(true)) {
            FreeEvent_ = NewPromise<void>();
            if (ActiveInvocationCount_ == 0) {
                FreeEvent_.Set();
            }
        }
        return FreeEvent_;
    }

    void Resume() override
    {
        if (Suspended_.exchange(false)) {
            FreeEvent_.Reset();
            ScheduleMore();
        }
    }

private:
    std::atomic<bool> Suspended_ = {false};
    std::atomic<int> ActiveInvocationCount_ = {0};

    TLockFreeQueue<TClosure> Queue_;

    TPromise<void> FreeEvent_;

    // TODO(acid): Think how to merge this class with implementation in other invokers.
    class TInvocationGuard
    {
    public:
        explicit TInvocationGuard(TIntrusivePtr<TSuspendableInvoker> owner)
            : Owner_(std::move(owner))
        { }

        TInvocationGuard(TInvocationGuard&& other) = default;
        TInvocationGuard(const TInvocationGuard& other) = delete;

        ~TInvocationGuard()
        {
            if (Owner_) {
                Owner_->OnFinished();
            }
        }

    private:
        TIntrusivePtr<TSuspendableInvoker> Owner_;

    };


    void RunCallback(TClosure callback, TInvocationGuard /*invocationGuard*/)
    {
        // Avoid deadlock caused by WaitFor in callback invoked in suspended invoker.
        TCurrentInvokerGuard guard(UnderlyingInvoker_);
        callback.Run();
    }

    void OnFinished()
    {
        YCHECK(ActiveInvocationCount_ > 0);

        if (--ActiveInvocationCount_ == 0 && Suspended_ && FreeEvent_) {
            FreeEvent_.Set();
        }
    }

    void ScheduleMore()
    {
        while (!Suspended_) {
            ++ActiveInvocationCount_;
            TInvocationGuard guard(this);

            TClosure callback;
            if (Suspended_ || !Queue_.Dequeue(&callback)) {
                break;
            }

            UnderlyingInvoker_->Invoke(BIND(
               &TSuspendableInvoker::RunCallback,
               MakeStrong(this),
               Passed(std::move(callback)),
               Passed(std::move(guard))));
        }
    }
};

ISuspendableInvokerPtr CreateSuspendableInvoker(IInvokerPtr underlyingInvoker)
{
    return New<TSuspendableInvoker>(underlyingInvoker);
}

///////////////////////////////////////////////////////////////////////////////

} // namespace NConcurrency
} // namespace NYT

