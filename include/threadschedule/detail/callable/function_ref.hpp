#pragma once

/**
 * @file detail/callable/function_ref.hpp
 * @brief Non-owning callable reference used by internal synchronous paths.
 */

#include "move_callable.hpp"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace threadschedule::detail
{

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

  template <typename F, typename = std::enable_if_t<!std::is_same_v<remove_cvref_t<F>, function_ref>
                                                    && std::is_invocable_r_v<R, F&, Args...>>>
  function_ref(F&& fn) noexcept : object_(const_cast<void*>(static_cast<void const*>(std::addressof(fn))))
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

} // namespace threadschedule::detail
