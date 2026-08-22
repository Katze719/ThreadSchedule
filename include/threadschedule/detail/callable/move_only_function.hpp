#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace threadschedule::detail
{

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename Signature, std::size_t InlineSize = 3 * sizeof(void*)>
class move_only_function;

template <typename R, typename... Args, std::size_t InlineSize>
class move_only_function<R(Args...), InlineSize>
{
  static constexpr std::size_t storage_size = InlineSize < sizeof(void*) ? sizeof(void*) : InlineSize;

  struct operations
  {
    auto (*invoke)(void*, Args&&...) -> R;
    void (*destroy)(void*) noexcept;
    void (*move)(void*, void*) noexcept;
  };

  template <typename F>
  static constexpr bool stores_inline
      = InlineSize != 0 && sizeof(F) <= storage_size && alignof(F) <= alignof(std::max_align_t)
        && std::is_nothrow_move_constructible_v<F>;

  template <typename F>
  static auto
  table() noexcept -> operations const*
  {
    if constexpr (stores_inline<F>)
      {
        static constexpr operations value{ [](void* storage, Args&&... args) -> R
                                             {
                                               if constexpr (std::is_void_v<R>)
                                                 std::invoke(*static_cast<F*>(storage), std::forward<Args>(args)...);
                                               else
                                                 return std::invoke(*static_cast<F*>(storage),
                                                                    std::forward<Args>(args)...);
                                             },
                                           [](void* storage) noexcept { static_cast<F*>(storage)->~F(); },
                                           [](void* destination, void* source) noexcept
                                             {
                                               ::new (destination) F(std::move(*static_cast<F*>(source)));
                                               static_cast<F*>(source)->~F();
                                             } };
        return &value;
      }
    else
      {
        static constexpr operations value{ [](void* storage, Args&&... args) -> R
                                             {
                                               auto& callable = **std::launder(static_cast<F**>(storage));
                                               if constexpr (std::is_void_v<R>)
                                                 std::invoke(callable, std::forward<Args>(args)...);
                                               else
                                                 return std::invoke(callable, std::forward<Args>(args)...);
                                             },
                                           [](void* storage) noexcept
                                             {
                                               auto pointer = std::launder(static_cast<F**>(storage));
                                               delete *pointer;
                                               *pointer = nullptr;
                                             },
                                           [](void* destination, void* source) noexcept
                                             {
                                               auto source_pointer = std::launder(static_cast<F**>(source));
                                               ::new (destination) F*(*source_pointer);
                                               *source_pointer = nullptr;
                                             } };
        return &value;
      }
  }

public:
  move_only_function() noexcept = default;
  move_only_function(std::nullptr_t) noexcept {} // NOLINT(google-explicit-constructor)

  template <typename F, typename Value = std::decay_t<F>,
            std::enable_if_t<!std::is_same_v<remove_cvref_t<F>, move_only_function>
                                 && std::is_invocable_r_v<R, Value&, Args...>,
                             int> = 0>
  move_only_function(F&& function) // NOLINT(google-explicit-constructor)
  {
    operations_ = table<Value>();
    if constexpr (stores_inline<Value>)
      ::new (storage_) Value(std::forward<F>(function));
    else
      ::new (storage_) Value*(new Value(std::forward<F>(function)));
  }

  move_only_function(move_only_function const&) = delete;
  auto operator=(move_only_function const&) -> move_only_function& = delete;

  move_only_function(move_only_function&& other) noexcept : operations_(other.operations_)
  {
    if (operations_ != nullptr)
      {
        operations_->move(storage_, other.storage_);
        other.operations_ = nullptr;
      }
  }

  auto
  operator=(move_only_function&& other) noexcept -> move_only_function&
  {
    if (this != &other)
      {
        reset();
        operations_ = other.operations_;
        if (operations_ != nullptr)
          {
            operations_->move(storage_, other.storage_);
            other.operations_ = nullptr;
          }
      }
    return *this;
  }

  ~move_only_function()
  {
    reset();
  }

  explicit
  operator bool() const noexcept
  {
    return operations_ != nullptr;
  }

  auto
  operator()(Args... args) -> R
  {
    if (operations_ == nullptr)
      throw std::bad_function_call();
    if constexpr (std::is_void_v<R>)
      operations_->invoke(storage_, std::forward<Args>(args)...);
    else
      return operations_->invoke(storage_, std::forward<Args>(args)...);
  }

  void
  reset() noexcept
  {
    if (operations_ != nullptr)
      {
        operations_->destroy(storage_);
        operations_ = nullptr;
      }
  }

private:
  operations const* operations_{ nullptr };
  alignas(std::max_align_t) unsigned char storage_[storage_size]{};
};

template <typename Signature, std::size_t InlineSize = 3 * sizeof(void*), typename Callable>
[[nodiscard]] auto
make_move_only_function(Callable&& callable) -> move_only_function<Signature, InlineSize>
{
  return move_only_function<Signature, InlineSize>(std::forward<Callable>(callable));
}

} // namespace threadschedule::detail
