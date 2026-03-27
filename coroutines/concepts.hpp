#ifndef ASYNC_LAB_CONCEPTS_HPP
#define ASYNC_LAB_CONCEPTS_HPP

#include <concepts>
#include <coroutine>

namespace AsyncLab
{
    template <typename T, typename... Ts>
    concept OneOf = (std::same_as<T, Ts> || ...);

    template <typename, template <typename...> class>
    constexpr bool is_instance_of_ = false;

    template <class... Ts, template <class...> class U>
    constexpr bool is_instance_of_<U<Ts...>, U> = true;

    template <class T, template <class...> class U>
    concept IsInstanceOf = is_instance_of_<T, U>;

    static_assert(IsInstanceOf<std::coroutine_handle<>, std::coroutine_handle>);
    static_assert(!IsInstanceOf<int, std::coroutine_handle>);

    template <typename T, template <class...> class U>
    concept IsNotInstanceOf = !IsInstanceOf<T, U>;

    //////////////////////////////////////////////////////////////
    // Awaiter concept
    //////////////////////////////////////////////////////////////

    template <typename T>
    concept AwaitSuspendResult = OneOf<T, void, bool> || IsInstanceOf<T, std::coroutine_handle>;

    template <typename TAwaiter, typename TPromise>
    concept WithAwaitSuspend = requires(TAwaiter awaiter, std::coroutine_handle<TPromise> coro_handle) {
        {
            awaiter.await_suspend(coro_handle)
        } -> AwaitSuspendResult;
    };

    // clang-format off
    template <typename TAwaiter, typename TPromise = void>
    concept Awaiter = requires(TAwaiter& awaiter) {
        { awaiter.await_ready() } -> std::same_as<bool>;
        { awaiter.await_resume() };
    } && WithAwaitSuspend<TAwaiter, TPromise>;
    // clang-format on

    static_assert(Awaiter<std::suspend_always>);
    static_assert(Awaiter<std::suspend_never>);

    /////////////////////////////////////////////////////////////////
    // __get_awaiter - returns the awaiter for a given awaitable, following the standard co_await resolution rules

    template <typename TAwaitable>
    decltype(auto) __get_awaiter(TAwaitable&& awaitable, void*)
    {
        if constexpr (requires { static_cast<TAwaitable&&>(awaitable).operator co_await(); })
        {
            return static_cast<TAwaitable&&>(awaitable).operator co_await();
        }
        else if constexpr (requires { operator co_await(static_cast<TAwaitable&&>(awaitable)); })
        {
            return operator co_await(static_cast<TAwaitable&&>(awaitable));
        }
        else
        {
            return static_cast<TAwaitable&&>(awaitable);
        }
    }

    template <typename TAwaitable, typename TPromise>
    decltype(auto) __get_awaiter(TAwaitable&& awaitable, TPromise* promise)
        requires requires { promise->await_transform(static_cast<TAwaitable&&>(awaitable)); }
    {
        if constexpr (requires { promise->await_transform(static_cast<TAwaitable&&>(awaitable)).operator co_await(); })
        {
            return promise->await_transform(static_cast<TAwaitable&&>(awaitable)).operator co_await();
        }
        else if constexpr (requires { operator co_await(promise->await_transform(static_cast<TAwaitable&&>(awaitable))); })
        {
            return operator co_await(promise->await_transform(static_cast<TAwaitable&&>(awaitable)));
        }
        else
        {
            return promise->await_transform(static_cast<TAwaitable&&>(awaitable));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Awaitable concept - types that can be co_awaited
    //////////////////////////////////////////////////////////////////////////

    template <typename TAwaitable, typename TPromise = void>
    concept Awaitable = requires(TAwaitable&& awaitable, TPromise* promise) {
        { __get_awaiter(static_cast<TAwaitable&&>(awaitable), promise) } -> Awaiter<TPromise>;
    };

    static_assert(Awaitable<std::suspend_always>);
    static_assert(Awaitable<std::suspend_never>);

    template <typename TAwaitable, typename TPromise = void>
    // requires Awaitable<TAwaitable, TPromise>
    using Awaiter_t = std::remove_cvref_t<decltype(__get_awaiter(std::declval<TAwaitable>(), static_cast<TPromise*>(nullptr)))>;

    template <class T>
    auto AsLValue(T&&) -> T&;

    template <typename TAwaitable, typename TPromise = void>
        requires Awaitable<TAwaitable, TPromise>
    using AwaitResult_t = decltype(AsLValue(__get_awaiter(std::declval<TAwaitable>(), static_cast<TPromise*>(nullptr))).await_resume());

    static_assert(std::same_as<AwaitResult_t<std::suspend_always>, void>);
    static_assert(std::same_as<AwaitResult_t<std::suspend_never>, void>);

} // namespace AsyncLab

#endif // ASYNC_LAB_CONCEPTS_HPP