#pragma once

#include "pthread_wrapper.hpp"
#include "thread_registry.hpp"
#include "thread_wrapper.hpp"
#include <functional>
#include <string>
#include <type_traits>
#include <utility>

namespace threadschedule
{

/**
 * @brief @ref ThreadWrapper with automatic registration in the global @ref ThreadRegistry.
 *
 * Non-copyable, movable. On thread start the spawned thread
 * auto-registers itself in the global registry() (via an
 * @ref AutoRegisterCurrentThread RAII guard) and auto-unregisters when
 * the thread function returns. The @p name and @p componentTag
 * arguments are forwarded to the registry entry.
 */
class ThreadWrapperReg : public ThreadWrapper
{
  public:
    ThreadWrapperReg() = default;

    ThreadWrapperReg(ThreadWrapperReg&&) noexcept = default;
    auto operator=(ThreadWrapperReg&&) noexcept -> ThreadWrapperReg& = default;

    ThreadWrapperReg(ThreadWrapperReg const&) = delete;
    auto operator=(ThreadWrapperReg const&) -> ThreadWrapperReg& = delete;

    template <typename F, typename... Args>
    explicit ThreadWrapperReg(std::string name, std::string componentTag, F&& f, Args&&... args)
        : ThreadWrapper(
              [n = std::move(name), c = std::move(componentTag), func = std::forward<F>(f)](auto&&... inner) {
                  AutoRegisterCurrentThread guard(n, c);
                  std::invoke(func, std::forward<decltype(inner)>(inner)...);
              },
              std::forward<Args>(args)...)
    {
    }
};

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
/**
 * @brief @ref JThreadWrapper with automatic registration in the global @ref ThreadRegistry.
 *
 * Non-copyable, movable. C++20 only. Behaves like @ref ThreadWrapperReg
 * but wraps a @c std::jthread and handles @c std::stop_token
 * forwarding: the callable may accept a @c stop_token as its first
 * argument, its last argument, or not at all -- all three signatures
 * are detected at compile time and dispatched accordingly.
 */
class JThreadWrapperReg : public JThreadWrapper
{
  public:
    JThreadWrapperReg() = default;

    JThreadWrapperReg(JThreadWrapperReg&&) noexcept = default;
    auto operator=(JThreadWrapperReg&&) noexcept -> JThreadWrapperReg& = default;

    JThreadWrapperReg(JThreadWrapperReg const&) = delete;
    auto operator=(JThreadWrapperReg const&) -> JThreadWrapperReg& = delete;

    template <typename F, typename... Args>
    explicit JThreadWrapperReg(std::string name, std::string componentTag, F&& f, Args&&... args)
        : JThreadWrapper(
              [n = std::move(name), c = std::move(componentTag),
               func = std::forward<F>(f)](std::stop_token st, auto&&... inner) {
                  AutoRegisterCurrentThread guard(n, c);
                  if constexpr (std::is_invocable_v<decltype(func), std::stop_token,
                                                    decltype(inner)...>)
                      std::invoke(func, std::move(st), std::forward<decltype(inner)>(inner)...);
                  else if constexpr (std::is_invocable_v<decltype(func), decltype(inner)...,
                                                         std::stop_token>)
                      std::invoke(func, std::forward<decltype(inner)>(inner)..., std::move(st));
                  else
                      std::invoke(func, std::forward<decltype(inner)>(inner)...);
              },
              std::forward<Args>(args)...)
    {
    }
};
#endif

#ifndef _WIN32
/**
 * @brief @ref PThreadWrapper with automatic registration in the global @ref ThreadRegistry.
 *
 * Non-copyable, movable. Linux-only (guarded by @c _WIN32).
 * Same auto-register / auto-unregister semantics as @ref ThreadWrapperReg,
 * but for POSIX threads.
 */
class PThreadWrapperReg : public PThreadWrapper
{
  public:
    PThreadWrapperReg() = default;

    PThreadWrapperReg(PThreadWrapperReg&&) noexcept = default;
    auto operator=(PThreadWrapperReg&&) noexcept -> PThreadWrapperReg& = default;

    PThreadWrapperReg(PThreadWrapperReg const&) = delete;
    auto operator=(PThreadWrapperReg const&) -> PThreadWrapperReg& = delete;

    template <typename F, typename... Args>
    explicit PThreadWrapperReg(std::string name, std::string componentTag, F&& f, Args&&... args)
        : PThreadWrapper(
              [n = std::move(name), c = std::move(componentTag), func = std::forward<F>(f)](auto&&... inner) {
                  AutoRegisterCurrentThread guard(n, c);
                  std::invoke(func, std::forward<decltype(inner)>(inner)...);
              },
              std::forward<Args>(args)...)
    {
    }
};
#endif

} // namespace threadschedule
