#pragma once

/**
 * @file callable.hpp
 * @brief ABI-consistent C++17 callable storage helpers.
 */

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#if defined(__has_include)
#  if __has_include(<version>)
#    include <version>
#  endif
#endif

namespace threadschedule
{
namespace detail
{

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename Signature>
class move_callable;

/**
 * @brief Library-owned C++17 move-only callable storage.
 *
 * Unlike std::function this accepts move-only targets. Keeping the type under
 * ThreadSchedule's control also prevents its representation from changing
 * when a consumer switches between C++17 and a standard library that provides
 * std::move_only_function.
 */
template <typename R, typename... Args>
class move_callable<R(Args...)>
{
  struct callable_base
  {
    virtual ~callable_base() = default;
    virtual auto invoke(Args... args) -> R = 0;
  };

  template <typename Callable>
  struct callable_model final : callable_base
  {
    template <typename Value>
    explicit callable_model(Value&& value)
        : callable_(std::forward<Value>(value))
    {
    }

    auto
    invoke(Args... args) -> R override
    {
      if constexpr (std::is_void_v<R>)
        {
          std::invoke(callable_, std::forward<Args>(args)...);
          return;
        }
      else
        {
          return std::invoke(callable_, std::forward<Args>(args)...);
        }
    }

    Callable callable_;
  };

public:
  move_callable() noexcept = default;
  move_callable(std::nullptr_t) noexcept {
  } // NOLINT(google-explicit-constructor)

  template <
      typename Callable,
      std::enable_if_t<
          !std::is_same_v<remove_cvref_t<Callable>, move_callable>
              && std::is_invocable_r_v<R, std::decay_t<Callable>&, Args...>,
          int> = 0>
  move_callable(Callable&& callable) // NOLINT(google-explicit-constructor)
      : callable_(std::make_unique<callable_model<std::decay_t<Callable>>>(
            std::forward<Callable>(callable)))
  {
  }

  move_callable(move_callable&&) noexcept = default;
  auto operator=(move_callable&&) noexcept -> move_callable& = default;
  move_callable(move_callable const&) = delete;
  auto operator=(move_callable const&) -> move_callable& = delete;

  explicit
  operator bool() const noexcept
  {
    return callable_ != nullptr;
  }

  auto
  operator()(Args... args) -> R
  {
    if (!callable_)
      throw std::bad_function_call();

    if constexpr (std::is_void_v<R>)
      {
        callable_->invoke(std::forward<Args>(args)...);
        return;
      }
    else
      {
        return callable_->invoke(std::forward<Args>(args)...);
      }
  }

private:
  std::unique_ptr<callable_base> callable_;
};

template <typename Signature>
using copyable_callable = std::function<Signature>;

template <typename Signature>
class function_ref;

template <typename R, typename... Args>
class function_ref<R(Args...)>
{
public:
  function_ref() = delete;

  function_ref(R (*fn)(Args...)) noexcept : function_(fn)
  {
    callback_ = [](void*, R (*function)(Args...), Args... args) -> R
      {
        if constexpr (std::is_void_v<R>)
          {
            function(std::forward<Args>(args)...);
            return;
          }
        else
          {
            return function(std::forward<Args>(args)...);
          }
      };
  }

  template <typename F,
            typename
            = std::enable_if_t<!std::is_same_v<remove_cvref_t<F>, function_ref>
                               && std::is_invocable_r_v<R, F&, Args...>>>
  function_ref(F&& fn) noexcept
      : object_(
            const_cast<void*>(static_cast<void const*>(std::addressof(fn))))
  {
    callback_ = [](void* object, R (*)(Args...), Args... args) -> R
      {
        auto& callable = *static_cast<remove_cvref_t<F>*>(object);
        if constexpr (std::is_void_v<R>)
          {
            callable(std::forward<Args>(args)...);
            return;
          }
        else
          {
            return callable(std::forward<Args>(args)...);
          }
      };
  }

  auto
  operator()(Args... args) const -> R
  {
    if constexpr (std::is_void_v<R>)
      {
        callback_(object_, function_, std::forward<Args>(args)...);
        return;
      }
    else
      {
        return callback_(object_, function_, std::forward<Args>(args)...);
      }
  }

private:
  void* object_ = nullptr;
  R (*function_)(Args...) = nullptr;
  R (*callback_)(void*, R (*)(Args...), Args...);
};
template <typename Signature, typename Callable>
auto
make_move_callable(Callable&& callable) -> move_callable<Signature>
{
  return move_callable<Signature>(std::forward<Callable>(callable));
}

template <typename Signature, typename Callable>
auto
make_copyable_callable(Callable&& callable) -> copyable_callable<Signature>
{
  return copyable_callable<Signature>(std::forward<Callable>(callable));
}

} // namespace detail
} // namespace threadschedule
