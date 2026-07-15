#pragma once

/**
 * @file detail/registered_thread_backend.hpp
 * @brief Thread wrappers with automatic global registry registration.
 */

#include "../thread_registry.hpp"
#include "thread_backend.hpp"
#include <functional>
#include <string>
#include <type_traits>
#include <utility>

namespace threadschedule
{

namespace detail
{

/**
 * @brief @ref thread_backend with automatic registration in the global @ref
 * thread_registry_backend.
 *
 * Non-copyable, movable. On thread start the spawned thread
 * auto-registers itself in the global registry() (via an
 * @ref auto_register_current_thread RAII guard) and auto-unregisters when
 * the thread function returns. The @p name and @p component
 * arguments are forwarded to the registry entry.
 */
class registered_thread_backend : public thread_backend
{
public:
  registered_thread_backend() = default;

  registered_thread_backend(registered_thread_backend&&) noexcept = default;
  auto operator=(registered_thread_backend&&) noexcept
      -> registered_thread_backend& = default;

  registered_thread_backend(registered_thread_backend const&) = delete;
  auto operator=(registered_thread_backend const&)
      -> registered_thread_backend& = delete;

  template <typename F, typename... Args>
  explicit registered_thread_backend(std::string name, std::string component,
                                     F&& f, Args&&... args)
      : thread_backend(
            [n = std::move(name), c = std::move(component),
             func = std::forward<F>(f)](auto&&... inner)
              {
                auto_register_current_thread guard(n, c);
                std::invoke(func, std::forward<decltype(inner)>(inner)...);
              },
            std::forward<Args>(args)...)
  {
  }
};

} // namespace detail

} // namespace threadschedule
