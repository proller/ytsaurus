// WARNING: This file was auto-generated.
// Please, consider incorporating any changes into the generator.

// Generated on Wed Oct 16 13:07:52 2013.


#pragma once

/*
//==============================================================================
// The following code is merely an adaptation of Chromium's Binds and Callbacks.
// Kudos to Chromium authors.
//
// Original Chromium revision:
//   - git-treeish: 206a2ae8a1ebd2b040753fff7da61bbca117757f
//   - git-svn-id:  svn://svn.chromium.org/chrome/trunk/src@115607
//
// See bind.h for an extended commentary.
//==============================================================================
*/
// NOTE: Header files that do not require the full definition of #TCallback<> or
// #TClosure should include "callback_forward.h" instead of this file.

///////////////////////////////////////////////////////////////////////////////
//
// WHAT IS THIS
//
// The templated #TCallback<> class is a generalized function object.
// Together with the #Bind() function in "bind.h" they provide a type-safe
// method for performing currying of arguments and creating a "closure".
//
// In programming languages, a closure is a first-class function where all its
// parameters have been bound (usually via currying). Closures are well-suited
// for representing and passing around a unit of delayed execution.
//
//
// MEMORY MANAGEMENT AND PASSING
//
// The #TCallback<> objects themselves should be passed by const reference, and
// stored by copy. They internally store their state in a reference-counted
// class and thus do not need to be deleted.
//
// The reason to pass via a const reference is to avoid unnecessary
// Ref/Unref pairs to the internal state.
//
// However, the #TCallback<> have Ref/Unref-efficient move constructors and
// assignment operators so they also may be efficiently moved.
//
//
// EXAMPLE USAGE
//
// (see "bind_ut.cpp")
//
//
// HOW THE IMPLEMENTATION WORKS:
//
// There are three main components to the system:
//   1) The #TCallback<> classes.
//   2) The #Bind() functions.
//   3) The arguments wrappers (e.g., #Unretained() and #ConstRef()).
//
// The #TCallback<> classes represent a generic function pointer. Internally,
// it stores a reference-counted piece of state that represents the target
// function and all its bound parameters. Each #TCallback<> specialization has
// a templated constructor that takes an #TBindState<>*. In the context of
// the constructor, the static type of this #TBindState<> pointer uniquely
// identifies the function it is representing, all its bound parameters,
// and a Run() method that is capable of invoking the target.
//
// #TCallback<>'s constructor takes the #TBindState<>* that has the full static
// type and erases the target function type as well as the types of the bound
// parameters. It does this by storing a pointer to the specific Run()
// function, and upcasting the state of #TBindState<>* to a #TBindStateBase*.
// This is safe as long as this #TBindStateBase pointer is only used with
// the stored Run() pointer.
//
// To #TBindState<> objects are created inside the #Bind() functions.
// These functions, along with a set of internal templates, are responsible for:
//
//   - Unwrapping the function signature into return type, and parameters,
//   - Determining the number of parameters that are bound,
//   - Creating the #TBindState<> storing the bound parameters,
//   - Performing compile-time asserts to avoid error-prone behavior,
//   - Returning a #TCallback<> with an arity matching the number of unbound
//     parameters and that knows the correct reference counting semantics for
//     the target object if we are binding a method.
//
// The #Bind() functions do the above using type-inference, and template
// specializations.
//
// By default #Bind() will store copies of all bound parameters, and attempt
// to reference count a target object if the function being bound is
// a class method.
//
// To change this behavior, we introduce a set of argument wrappers
// (e.g., #Unretained(), and #ConstRef()). These are simple container templates
// that are passed by value, and wrap a pointer to an argument.
// See the file-level comment in "bind_helpers.h" for more information.
//
// These types are passed to #Unwrap() functions, and #TMaybeRefCountHelper()
// functions respectively to modify the behavior of #Bind(). #Unwrap()
// and #TMaybeRefCountHelper() functions change behavior by doing partial
// specialization based on whether or not a parameter is a wrapper type.
//
// #ConstRef() is similar to #tr1::cref().
// #Unretained() is specific.
//
////////////////////////////////////////////////////////////////////////////////

#include "callback_forward.h"
#include "callback_internal.h"

#include <ytlib/misc/mpl.h>

#ifdef ENABLE_BIND_LOCATION_TRACKING
#include <ytlib/misc/source_location.h>
#endif

namespace NYT {

// TODO(sandello): Replace these with a proper include with forward decls.
template <class T>
class TFuture;

template <>
class TFuture<void>;

template <class T>
class TPromise;

template <>
class TPromise<void>;

struct IInvoker;

template <class T>
TPromise<T> NewPromise();

/*! \internal */
////////////////////////////////////////////////////////////////////////////////
//
// First, we forward declare the #TCallback<> class template. This informs the
// compiler that the template only has 1 type parameter which is the function
// signature that the #TCallback<> is representing.
//
// After this, create template specializations for 0-7 parameters. Note that
// even though the template type list grows, the specialization still has
// only one type: the function signature.
//
// If you are thinking of forward declaring #TCallback<> in your own header
// file,
// please include "callback_forward.h" instead.
//

template <class Signature>
class TCallback;

namespace NDetail {

template <class Runnable, class Signature, class BoundArgs>
class TBindState;

// TODO(sandello): Move these somewhere closer to TFuture & TPromise.
template <class R>
struct TFutureHelper
{
    typedef TFuture<R> TFutureType;
    typedef TPromise<R> TPromiseType;
    typedef R TValueType;
    enum
    {
        WrappedInFuture = 0
    };
};

template <class R>
struct TFutureHelper< TFuture<R> >
{
    typedef TFuture<R> TFutureType;
    typedef TPromise<R> TPromiseType;
    typedef R TValueType;
    enum
    {
        WrappedInFuture = 1
    };
};

template <class R>
struct TFutureHelper< TPromise<R> >
{
    typedef TFuture<R> TFutureType;
    typedef TPromise<R> TPromiseType;
    typedef R TValueType;
    enum
    {
        WrappedInFuture = 1
    };
};

} // namespace NDetail

template <class R>
class TCallback<R()>
    : public NYT::NDetail::TCallbackBase
{
public:
    typedef R(Signature)();

    TCallback()
        : TCallbackBase(TIntrusivePtr< NYT::NDetail::TBindStateBase >())
    { }

    TCallback(const TCallback& other)
        : TCallbackBase(other)
    { }

    TCallback(TCallback&& other)
        : TCallbackBase(std::move(other))
    { }

    template <class Runnable, class Signature, class BoundArgs>
    explicit TCallback(TIntrusivePtr<
            NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
        >&& bindState)
        : TCallbackBase(std::move(bindState))
    {
        // Force the assignment to a local variable of TTypedInvokeFunction
        // so the compiler will typecheck that the passed in Run() method has
        // the correct type.
        TTypedInvokeFunction invokeFunction =
            &NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
            ::TInvokerType::Run;
        UntypedInvoke =
            reinterpret_cast<TUntypedInvokeFunction>(invokeFunction);
    }

    using TCallbackBase::Equals;

    TCallback& operator=(const TCallback& other)
    {
        TCallback(other).Swap(*this);
        return *this;
    }

    TCallback& operator=(TCallback&& other)
    {
        TCallback(std::move(other)).Swap(*this);
        return *this;
    }

    R Run() const
    {
        TTypedInvokeFunction invokeFunction =
            reinterpret_cast<TTypedInvokeFunction>(UntypedInvoke);
        return invokeFunction(BindState.Get());
    }

    // XXX(sandello): This is legacy. Due to forced migration to new callbacks.
    TCallback Via(
        TIntrusivePtr<IInvoker> invoker);
    TCallback<typename NYT::NDetail::TFutureHelper<R>::TFutureType()>
    AsyncVia(TIntrusivePtr<IInvoker> invoker);

private:
    typedef R(*TTypedInvokeFunction)(
        NYT::NDetail::TBindStateBase*);

};

template <class R, class A1>
class TCallback<R(A1)>
    : public NYT::NDetail::TCallbackBase
{
public:
    typedef R(Signature)(A1);

    TCallback()
        : TCallbackBase(TIntrusivePtr< NYT::NDetail::TBindStateBase >())
    { }

    TCallback(const TCallback& other)
        : TCallbackBase(other)
    { }

    TCallback(TCallback&& other)
        : TCallbackBase(std::move(other))
    { }

    template <class Runnable, class Signature, class BoundArgs>
    explicit TCallback(TIntrusivePtr<
            NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
        >&& bindState)
        : TCallbackBase(std::move(bindState))
    {
        // Force the assignment to a local variable of TTypedInvokeFunction
        // so the compiler will typecheck that the passed in Run() method has
        // the correct type.
        TTypedInvokeFunction invokeFunction =
            &NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
            ::TInvokerType::Run;
        UntypedInvoke =
            reinterpret_cast<TUntypedInvokeFunction>(invokeFunction);
    }

    using TCallbackBase::Equals;

    TCallback& operator=(const TCallback& other)
    {
        TCallback(other).Swap(*this);
        return *this;
    }

    TCallback& operator=(TCallback&& other)
    {
        TCallback(std::move(other)).Swap(*this);
        return *this;
    }

    R Run(A1 a1) const
    {
        TTypedInvokeFunction invokeFunction =
            reinterpret_cast<TTypedInvokeFunction>(UntypedInvoke);
        return invokeFunction(BindState.Get(),
            std::forward<A1>(a1));
    }

    // XXX(sandello): This is legacy. Due to forced migration to new callbacks.
    TCallback Via(
        TIntrusivePtr<IInvoker> invoker);
    TCallback<typename NYT::NDetail::TFutureHelper<R>::TFutureType(A1)>
    AsyncVia(TIntrusivePtr<IInvoker> invoker);

private:
    typedef R(*TTypedInvokeFunction)(
        NYT::NDetail::TBindStateBase*, A1&&);

};

template <class R, class A1, class A2>
class TCallback<R(A1, A2)>
    : public NYT::NDetail::TCallbackBase
{
public:
    typedef R(Signature)(A1, A2);

    TCallback()
        : TCallbackBase(TIntrusivePtr< NYT::NDetail::TBindStateBase >())
    { }

    TCallback(const TCallback& other)
        : TCallbackBase(other)
    { }

    TCallback(TCallback&& other)
        : TCallbackBase(std::move(other))
    { }

    template <class Runnable, class Signature, class BoundArgs>
    explicit TCallback(TIntrusivePtr<
            NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
        >&& bindState)
        : TCallbackBase(std::move(bindState))
    {
        // Force the assignment to a local variable of TTypedInvokeFunction
        // so the compiler will typecheck that the passed in Run() method has
        // the correct type.
        TTypedInvokeFunction invokeFunction =
            &NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
            ::TInvokerType::Run;
        UntypedInvoke =
            reinterpret_cast<TUntypedInvokeFunction>(invokeFunction);
    }

    using TCallbackBase::Equals;

    TCallback& operator=(const TCallback& other)
    {
        TCallback(other).Swap(*this);
        return *this;
    }

    TCallback& operator=(TCallback&& other)
    {
        TCallback(std::move(other)).Swap(*this);
        return *this;
    }

    R Run(A1 a1, A2 a2) const
    {
        TTypedInvokeFunction invokeFunction =
            reinterpret_cast<TTypedInvokeFunction>(UntypedInvoke);
        return invokeFunction(BindState.Get(),
            std::forward<A1>(a1),
            std::forward<A2>(a2));
    }

    // XXX(sandello): This is legacy. Due to forced migration to new callbacks.
    TCallback Via(
        TIntrusivePtr<IInvoker> invoker);
    TCallback<typename NYT::NDetail::TFutureHelper<R>::TFutureType(A1, A2)>
    AsyncVia(TIntrusivePtr<IInvoker> invoker);

private:
    typedef R(*TTypedInvokeFunction)(
        NYT::NDetail::TBindStateBase*, A1&&, A2&&);

};

template <class R, class A1, class A2, class A3>
class TCallback<R(A1, A2, A3)>
    : public NYT::NDetail::TCallbackBase
{
public:
    typedef R(Signature)(A1, A2, A3);

    TCallback()
        : TCallbackBase(TIntrusivePtr< NYT::NDetail::TBindStateBase >())
    { }

    TCallback(const TCallback& other)
        : TCallbackBase(other)
    { }

    TCallback(TCallback&& other)
        : TCallbackBase(std::move(other))
    { }

    template <class Runnable, class Signature, class BoundArgs>
    explicit TCallback(TIntrusivePtr<
            NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
        >&& bindState)
        : TCallbackBase(std::move(bindState))
    {
        // Force the assignment to a local variable of TTypedInvokeFunction
        // so the compiler will typecheck that the passed in Run() method has
        // the correct type.
        TTypedInvokeFunction invokeFunction =
            &NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
            ::TInvokerType::Run;
        UntypedInvoke =
            reinterpret_cast<TUntypedInvokeFunction>(invokeFunction);
    }

    using TCallbackBase::Equals;

    TCallback& operator=(const TCallback& other)
    {
        TCallback(other).Swap(*this);
        return *this;
    }

    TCallback& operator=(TCallback&& other)
    {
        TCallback(std::move(other)).Swap(*this);
        return *this;
    }

    R Run(A1 a1, A2 a2, A3 a3) const
    {
        TTypedInvokeFunction invokeFunction =
            reinterpret_cast<TTypedInvokeFunction>(UntypedInvoke);
        return invokeFunction(BindState.Get(),
            std::forward<A1>(a1),
            std::forward<A2>(a2),
            std::forward<A3>(a3));
    }

    // XXX(sandello): This is legacy. Due to forced migration to new callbacks.
    TCallback Via(
        TIntrusivePtr<IInvoker> invoker);
    TCallback<typename NYT::NDetail::TFutureHelper<R>::TFutureType(A1, A2, A3)>
    AsyncVia(TIntrusivePtr<IInvoker> invoker);

private:
    typedef R(*TTypedInvokeFunction)(
        NYT::NDetail::TBindStateBase*, A1&&, A2&&, A3&&);

};

template <class R, class A1, class A2, class A3, class A4>
class TCallback<R(A1, A2, A3, A4)>
    : public NYT::NDetail::TCallbackBase
{
public:
    typedef R(Signature)(A1, A2, A3, A4);

    TCallback()
        : TCallbackBase(TIntrusivePtr< NYT::NDetail::TBindStateBase >())
    { }

    TCallback(const TCallback& other)
        : TCallbackBase(other)
    { }

    TCallback(TCallback&& other)
        : TCallbackBase(std::move(other))
    { }

    template <class Runnable, class Signature, class BoundArgs>
    explicit TCallback(TIntrusivePtr<
            NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
        >&& bindState)
        : TCallbackBase(std::move(bindState))
    {
        // Force the assignment to a local variable of TTypedInvokeFunction
        // so the compiler will typecheck that the passed in Run() method has
        // the correct type.
        TTypedInvokeFunction invokeFunction =
            &NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
            ::TInvokerType::Run;
        UntypedInvoke =
            reinterpret_cast<TUntypedInvokeFunction>(invokeFunction);
    }

    using TCallbackBase::Equals;

    TCallback& operator=(const TCallback& other)
    {
        TCallback(other).Swap(*this);
        return *this;
    }

    TCallback& operator=(TCallback&& other)
    {
        TCallback(std::move(other)).Swap(*this);
        return *this;
    }

    R Run(A1 a1, A2 a2, A3 a3, A4 a4) const
    {
        TTypedInvokeFunction invokeFunction =
            reinterpret_cast<TTypedInvokeFunction>(UntypedInvoke);
        return invokeFunction(BindState.Get(),
            std::forward<A1>(a1),
            std::forward<A2>(a2),
            std::forward<A3>(a3),
            std::forward<A4>(a4));
    }

    // XXX(sandello): This is legacy. Due to forced migration to new callbacks.
    TCallback Via(
        TIntrusivePtr<IInvoker> invoker);
    TCallback<typename NYT::NDetail::TFutureHelper<R>::TFutureType(A1, A2, A3,
        A4)>
    AsyncVia(TIntrusivePtr<IInvoker> invoker);

private:
    typedef R(*TTypedInvokeFunction)(
        NYT::NDetail::TBindStateBase*, A1&&, A2&&, A3&&, A4&&);

};

template <class R, class A1, class A2, class A3, class A4, class A5>
class TCallback<R(A1, A2, A3, A4, A5)>
    : public NYT::NDetail::TCallbackBase
{
public:
    typedef R(Signature)(A1, A2, A3, A4, A5);

    TCallback()
        : TCallbackBase(TIntrusivePtr< NYT::NDetail::TBindStateBase >())
    { }

    TCallback(const TCallback& other)
        : TCallbackBase(other)
    { }

    TCallback(TCallback&& other)
        : TCallbackBase(std::move(other))
    { }

    template <class Runnable, class Signature, class BoundArgs>
    explicit TCallback(TIntrusivePtr<
            NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
        >&& bindState)
        : TCallbackBase(std::move(bindState))
    {
        // Force the assignment to a local variable of TTypedInvokeFunction
        // so the compiler will typecheck that the passed in Run() method has
        // the correct type.
        TTypedInvokeFunction invokeFunction =
            &NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
            ::TInvokerType::Run;
        UntypedInvoke =
            reinterpret_cast<TUntypedInvokeFunction>(invokeFunction);
    }

    using TCallbackBase::Equals;

    TCallback& operator=(const TCallback& other)
    {
        TCallback(other).Swap(*this);
        return *this;
    }

    TCallback& operator=(TCallback&& other)
    {
        TCallback(std::move(other)).Swap(*this);
        return *this;
    }

    R Run(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5) const
    {
        TTypedInvokeFunction invokeFunction =
            reinterpret_cast<TTypedInvokeFunction>(UntypedInvoke);
        return invokeFunction(BindState.Get(),
            std::forward<A1>(a1),
            std::forward<A2>(a2),
            std::forward<A3>(a3),
            std::forward<A4>(a4),
            std::forward<A5>(a5));
    }

    // XXX(sandello): This is legacy. Due to forced migration to new callbacks.
    TCallback Via(
        TIntrusivePtr<IInvoker> invoker);
    TCallback<typename NYT::NDetail::TFutureHelper<R>::TFutureType(A1, A2, A3,
        A4, A5)>
    AsyncVia(TIntrusivePtr<IInvoker> invoker);

private:
    typedef R(*TTypedInvokeFunction)(
        NYT::NDetail::TBindStateBase*, A1&&, A2&&, A3&&, A4&&, A5&&);

};

template <class R, class A1, class A2, class A3, class A4, class A5, class A6>
class TCallback<R(A1, A2, A3, A4, A5, A6)>
    : public NYT::NDetail::TCallbackBase
{
public:
    typedef R(Signature)(A1, A2, A3, A4, A5, A6);

    TCallback()
        : TCallbackBase(TIntrusivePtr< NYT::NDetail::TBindStateBase >())
    { }

    TCallback(const TCallback& other)
        : TCallbackBase(other)
    { }

    TCallback(TCallback&& other)
        : TCallbackBase(std::move(other))
    { }

    template <class Runnable, class Signature, class BoundArgs>
    explicit TCallback(TIntrusivePtr<
            NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
        >&& bindState)
        : TCallbackBase(std::move(bindState))
    {
        // Force the assignment to a local variable of TTypedInvokeFunction
        // so the compiler will typecheck that the passed in Run() method has
        // the correct type.
        TTypedInvokeFunction invokeFunction =
            &NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
            ::TInvokerType::Run;
        UntypedInvoke =
            reinterpret_cast<TUntypedInvokeFunction>(invokeFunction);
    }

    using TCallbackBase::Equals;

    TCallback& operator=(const TCallback& other)
    {
        TCallback(other).Swap(*this);
        return *this;
    }

    TCallback& operator=(TCallback&& other)
    {
        TCallback(std::move(other)).Swap(*this);
        return *this;
    }

    R Run(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6) const
    {
        TTypedInvokeFunction invokeFunction =
            reinterpret_cast<TTypedInvokeFunction>(UntypedInvoke);
        return invokeFunction(BindState.Get(),
            std::forward<A1>(a1),
            std::forward<A2>(a2),
            std::forward<A3>(a3),
            std::forward<A4>(a4),
            std::forward<A5>(a5),
            std::forward<A6>(a6));
    }

    // XXX(sandello): This is legacy. Due to forced migration to new callbacks.
    TCallback Via(
        TIntrusivePtr<IInvoker> invoker);
    TCallback<typename NYT::NDetail::TFutureHelper<R>::TFutureType(A1, A2, A3,
        A4, A5, A6)>
    AsyncVia(TIntrusivePtr<IInvoker> invoker);

private:
    typedef R(*TTypedInvokeFunction)(
        NYT::NDetail::TBindStateBase*, A1&&, A2&&, A3&&, A4&&, A5&&, A6&&);

};

template <class R, class A1, class A2, class A3, class A4, class A5, class A6,
    class A7>
class TCallback<R(A1, A2, A3, A4, A5, A6, A7)>
    : public NYT::NDetail::TCallbackBase
{
public:
    typedef R(Signature)(A1, A2, A3, A4, A5, A6, A7);

    TCallback()
        : TCallbackBase(TIntrusivePtr< NYT::NDetail::TBindStateBase >())
    { }

    TCallback(const TCallback& other)
        : TCallbackBase(other)
    { }

    TCallback(TCallback&& other)
        : TCallbackBase(std::move(other))
    { }

    template <class Runnable, class Signature, class BoundArgs>
    explicit TCallback(TIntrusivePtr<
            NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
        >&& bindState)
        : TCallbackBase(std::move(bindState))
    {
        // Force the assignment to a local variable of TTypedInvokeFunction
        // so the compiler will typecheck that the passed in Run() method has
        // the correct type.
        TTypedInvokeFunction invokeFunction =
            &NYT::NDetail::TBindState<Runnable, Signature, BoundArgs>
            ::TInvokerType::Run;
        UntypedInvoke =
            reinterpret_cast<TUntypedInvokeFunction>(invokeFunction);
    }

    using TCallbackBase::Equals;

    TCallback& operator=(const TCallback& other)
    {
        TCallback(other).Swap(*this);
        return *this;
    }

    TCallback& operator=(TCallback&& other)
    {
        TCallback(std::move(other)).Swap(*this);
        return *this;
    }

    R Run(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7) const
    {
        TTypedInvokeFunction invokeFunction =
            reinterpret_cast<TTypedInvokeFunction>(UntypedInvoke);
        return invokeFunction(BindState.Get(),
            std::forward<A1>(a1),
            std::forward<A2>(a2),
            std::forward<A3>(a3),
            std::forward<A4>(a4),
            std::forward<A5>(a5),
            std::forward<A6>(a6),
            std::forward<A7>(a7));
    }

    // XXX(sandello): This is legacy. Due to forced migration to new callbacks.
    TCallback Via(
        TIntrusivePtr<IInvoker> invoker);
    TCallback<typename NYT::NDetail::TFutureHelper<R>::TFutureType(A1, A2, A3,
        A4, A5, A6, A7)>
    AsyncVia(TIntrusivePtr<IInvoker> invoker);

private:
    typedef R(*TTypedInvokeFunction)(
        NYT::NDetail::TBindStateBase*, A1&&, A2&&, A3&&, A4&&, A5&&, A6&&,
            A7&&);

};

// Syntactic sugar to make Callbacks<void()> easier to declare since it
// will be used in a lot of APIs with delayed execution.
typedef TCallback<void()> TClosure;

////////////////////////////////////////////////////////////////////////////////
/*! \endinternal */
} // namespace NYT

#include "bind.h"
#define CALLBACK_VIA_H_
#include "callback_via.h"
#undef CALLBACK_VIA_H_
