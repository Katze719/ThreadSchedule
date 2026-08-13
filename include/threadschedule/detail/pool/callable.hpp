#pragma once

/**
 * @file detail/pool/callable.hpp
 * @brief Pool-local move-only callable storage and task instrumentation.
 *
 * Internal implementation fragment included by backend.hpp inside
 * threadschedule::detail.
 */

/**
 * @par Storage layout
 * @code
 *   |<---------- TaskSize bytes ---------->|
 *   [ vtable* (8 B) | inline buffer        ]
 * @endcode
 * The usable inline buffer is @c TaskSize - sizeof(void*) bytes
 * (56 bytes on 64-bit platforms with the default @c TaskSize of 64).
 *
 * @par Inline eligibility
 * A callable @c F is stored inline when all of the following hold:
 * - @c sizeof(F) <= buffer_size
 * - @c alignof(F) <= alignof(std::max_align_t)
 * - @c std::is_nothrow_move_constructible_v<F>
 *
 * @par Move semantics
 * Move-only. Invoking @c operator() consumes the callable (invoke + destroy),
 * leaving the object in an empty state. This single-shot design avoids the
 * overhead of reference counting or shared ownership.
 *
 * @par Thread safety
 * Not thread-safe. Intended to be used as a queue element inside a
 * mutex-protected task queue.
 *
 * @tparam TaskSize Total object size in bytes (default 64, one x86 cache
 * line).
 */
template <size_t TaskSize = 64>
class sbo_callable
{
  static_assert(TaskSize > sizeof(void*), "TaskSize must be larger than a pointer");

  struct vtable
  {
    void (*invoke)(void* storage);
    void (*destroy)(void* storage);
    void (*move_to)(void* dst, void* src) noexcept;
  };

  static constexpr size_t buffer_size = TaskSize - sizeof(vtable const*);

  template <typename F>
  static constexpr bool fits_inline_v
      = sizeof(F) <= buffer_size && alignof(F) <= alignof(std::max_align_t) && std::is_nothrow_move_constructible_v<F>;

  template <typename F>
  static vtable const*
  vtable_for() noexcept
  {
    if constexpr (fits_inline_v<F>)
      {
        static constexpr vtable vt{ [](void* s) { (*static_cast<F*>(s))(); }, [](void* s) { static_cast<F*>(s)->~F(); },
                                    [](void* dst, void* src) noexcept
                                      {
                                        ::new (dst) F(std::move(*static_cast<F*>(src)));
                                        static_cast<F*>(src)->~F();
                                      } };
        return &vt;
      }
    else
      {
        static constexpr vtable vt{ [](void* s) { (*(*static_cast<F**>(s)))(); },
                                    [](void* s) { delete *static_cast<F**>(s); },
                                    [](void* dst, void* src) noexcept
                                      {
                                        *static_cast<F**>(dst) = *static_cast<F**>(src);
                                        *static_cast<F**>(src) = nullptr;
                                      } };
        return &vt;
      }
  }

public:
  sbo_callable() = default;

  template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, sbo_callable>>>
  sbo_callable(F&& f) // NOLINT(google-explicit-constructor)
  {
    using decay_type = std::decay_t<F>;
    vtable_ = vtable_for<decay_type>();
    if constexpr (fits_inline_v<decay_type>)
      ::new (buffer_) decay_type(std::forward<F>(f));
    else
      *reinterpret_cast<decay_type**>(buffer_) = new decay_type(std::forward<F>(f));
  }

  sbo_callable(sbo_callable&& other) noexcept : vtable_(other.vtable_)
  {
    if (vtable_)
      {
        vtable_->move_to(buffer_, other.buffer_);
        other.vtable_ = nullptr;
      }
  }

  auto
  operator=(sbo_callable&& other) noexcept -> sbo_callable&
  {
    if (this != &other)
      {
        if (vtable_)
          vtable_->destroy(buffer_);
        vtable_ = other.vtable_;
        if (vtable_)
          {
            vtable_->move_to(buffer_, other.buffer_);
            other.vtable_ = nullptr;
          }
      }
    return *this;
  }

  sbo_callable(sbo_callable const&) = delete;
  auto operator=(sbo_callable const&) -> sbo_callable& = delete;

  ~sbo_callable()
  {
    if (vtable_)
      vtable_->destroy(buffer_);
  }

  explicit
  operator bool() const noexcept
  {
    return vtable_ != nullptr;
  }

  void
  operator()()
  {
    auto* vt = vtable_;
    if (vt == nullptr)
      throw std::bad_function_call();
    vtable_ = nullptr;
    try
      {
        vt->invoke(buffer_);
      }
    catch (...)
      {
        vt->destroy(buffer_);
        throw;
      }
    vt->destroy(buffer_);
  }

private:
  vtable const* vtable_ = nullptr;
  alignas(std::max_align_t) unsigned char buffer_[buffer_size];
};

/**
 * @brief Work-stealing deque for per-thread task queues in a thread pool.
 *
 * Implements a double-ended queue where the owning worker thread pushes and
 * pops tasks from the top, while other ("thief") threads steal tasks from the
 * bottom. This asymmetry reduces contention under typical workloads because
 * the owner operates on one end and thieves on the other.
 *
 * @par Thread safety
 * All public operations are serialized by an internal mutex, so the deque is
 * safe to use concurrently from any number of threads. The atomic counters
 * (top_ / bottom_) exist for a fast, lock-free size() / empty() snapshot but
 * do @e not make push/pop/steal lock-free; the mutex is always acquired.
 *
 * @par Capacity
 * The deque has a fixed capacity set at construction (default
 * @c default_capacity = 1024). push() returns @c false when the deque is
 * full; it never reallocates. Choose a capacity large enough for your expected
 * burst size or use an overflow queue externally (as @ref
 * work_stealing_pool_backend does).
 *
 * @par Memory layout
 * Each stored item is wrapped in an @c aligned_item that is aligned to
 * @c cache_line_size (64 bytes) to prevent false sharing between adjacent
 * elements when multiple threads access neighboring slots.
 *
 * @par Copyability / movability
 * Not copyable and not movable (contains a std::mutex).
 *
 * @tparam T The task type. Must be move-constructible.
 */

/// Callback invoked when a pool worker begins executing a task.
using task_start_callback = detail::copyable_callable<void(std::chrono::steady_clock::time_point, std::thread::id)>;

/// Callback invoked when a pool worker finishes executing a task.
using task_end_callback = detail::copyable_callable<void(std::chrono::steady_clock::time_point, std::thread::id,
                                                         std::chrono::microseconds elapsed)>;

using task_start_callback_storage = task_start_callback;
using task_end_callback_storage = task_end_callback;
