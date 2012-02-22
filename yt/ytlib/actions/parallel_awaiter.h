#pragma once

#include "common.h"

#include "future.h"
#include "invoker_util.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class TParallelAwaiter
    : public TRefCounted
{
public:
    typedef TIntrusivePtr<TParallelAwaiter> TPtr;

    explicit TParallelAwaiter(IInvoker::TPtr invoker = TSyncInvoker::Get());

    template <class T>
    void Await(
        TIntrusivePtr< TFuture<T> > result,
        TIntrusivePtr< IParamAction<T> > onResult = NULL);

    void Complete(IAction::TPtr onComplete = NULL);
    void Cancel();
    bool IsCanceled() const;

private:
    TSpinLock SpinLock;
    bool Canceled;
    bool Completed;
    bool Terminated;
    i32 RequestCount;
    i32 ResponseCount;
    IAction::TPtr OnComplete;
    TCancelableInvoker::TPtr CancelableInvoker;

    void Terminate();

    template <class T>
    void OnResult(
        T result,
        typename IParamAction<T>::TPtr onResult);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

#define PARALLEL_AWAITER_INL_H_
#include "parallel_awaiter-inl.h"
#undef PARALLEL_AWAITER_INL_H_
