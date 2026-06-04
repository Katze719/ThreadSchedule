#pragma once

/**
 * @file callable.hpp
 * @brief Feature-gated callable storage aliases for modern C++ builds.
 */

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#if defined(__has_include)
#    if __has_include(<version>)
#        include <version>
#    endif
#endif

namespace threadschedule
{
namespace detail
{

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
template <typename Signature>
using move_callable = std::move_only_function<Signature>;
#else
template <typename Signature>
using move_callable = std::function<Signature>;
#endif

#if defined(__cpp_lib_copyable_function) && __cpp_lib_copyable_function >= 202306L
template <typename Signature>
using copyable_callable = std::copyable_function<Signature>;
#else
template <typename Signature>
using copyable_callable = std::function<Signature>;
#endif

#if defined(__cpp_lib_function_ref) && __cpp_lib_function_ref >= 202306L
template <typename Signature>
using function_ref = std::function_ref<Signature>;
#else
template <typename Signature>
class function_ref;

template <typename R, typename... Args>
class function_ref<R(Args...)>
{
  public:
    function_ref() = delete;

    function_ref(R (*fn)(Args...)) noexcept : function_(fn)
    {
        callback_ = [](void*, R (*function)(Args...), Args... args) -> R {
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
              typename = std::enable_if_t<!std::is_same_v<remove_cvref_t<F>, function_ref> &&
                                          std::is_invocable_r_v<R, F&, Args...>>>
    function_ref(F&& fn) noexcept : object_(const_cast<void*>(static_cast<void const*>(std::addressof(fn))))
    {
        callback_ = [](void* object, R (*)(Args...), Args... args) -> R {
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

    auto operator()(Args... args) const -> R
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
#endif

template <typename Signature, typename Callable>
auto make_move_callable(Callable&& callable) -> move_callable<Signature>
{
    return move_callable<Signature>(std::forward<Callable>(callable));
}

template <typename Signature, typename Callable>
auto make_copyable_callable(Callable&& callable) -> copyable_callable<Signature>
{
    return copyable_callable<Signature>(std::forward<Callable>(callable));
}

} // namespace detail
} // namespace threadschedule
