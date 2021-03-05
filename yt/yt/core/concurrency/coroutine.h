#pragma once

#include "public.h"
#include "execution_stack.h"

#include <yt/yt/core/actions/callback.h>

#include <yt/yt/core/misc/optional.h>

#include <util/system/context.h>

#include <type_traits>

namespace NYT::NConcurrency {

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

class TCoroutineBase
    : public ITrampoLine
{
protected:
    bool Completed_ = false;

    TExceptionSafeContext CallerContext_;

    std::shared_ptr<TExecutionStack> CoroutineStack_;
    TExceptionSafeContext CoroutineContext_;
    std::exception_ptr CoroutineException_;

    TCoroutineBase(const EExecutionStackKind stackKind);

    TCoroutineBase(const TCoroutineBase& other) = delete;
    TCoroutineBase& operator=(const TCoroutineBase& other) = delete;

    virtual ~TCoroutineBase() = default;

    virtual void Invoke() = 0;

    // ITrampoLine implementation
    virtual void DoRun();

    void JumpToCaller();
    void JumpToCoroutine();

public:
    bool IsCompleted() const;
};

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

template <class R, class... TArgs>
class TCoroutine<R(TArgs...)>
    : public NDetail::TCoroutineBase
{
public:
    using TCallee = TCallback<void(TCoroutine&, TArgs...)>;
    using TArguments = std::tuple<TArgs...>;

    TCoroutine() = default;
    TCoroutine(TCallee&& callee, const EExecutionStackKind stackKind = EExecutionStackKind::Small);

    template <class... TParams>
    const std::optional<R>& Run(TParams&&... params);

    template <class Q>
    TArguments&& Yield(Q&& result);

private:
    virtual void Invoke() override;

private:
    const TCallee Callee_;

    TArguments Arguments_;
    std::optional<R> Result_;
};

////////////////////////////////////////////////////////////////////////////////

template <class... TArgs>
class TCoroutine<void(TArgs...)>
    : public NDetail::TCoroutineBase
{
public:
    using TCallee = TCallback<void(TCoroutine&, TArgs...)>;
    using TArguments = std::tuple<TArgs...>;

    TCoroutine() = default;
    TCoroutine(TCallee&& callee, const EExecutionStackKind stackKind = EExecutionStackKind::Small);

    template <class... TParams>
    bool Run(TParams&&... params);

    TArguments&& Yield();

private:
    virtual void Invoke() override;

private:
    const TCallee Callee_;

    TArguments Arguments_;
    bool Result_ = false;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NConcurrency

#define COROUTINE_INL_H_
#include "coroutine-inl.h"
#undef COROUTINE_INL_H_
