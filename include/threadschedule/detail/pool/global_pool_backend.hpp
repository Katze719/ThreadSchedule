#pragma once

/**
 * @file detail/pool/global_pool_backend.hpp
 * @brief Process-wide pool facades and global parallel helpers.
 *
 * Self-contained internal implementation header.
 */

#include "thread_pool_backend_base.hpp"
#include "work_stealing_pool_backend.hpp"
#include "worker_count.hpp"

#include <cstddef>
#include <mutex>
#include <thread>
#include <utility>

namespace threadschedule::detail
{

// ---------------------------------------------------------------------------
// global_pool_backend
// ---------------------------------------------------------------------------

/**
 * @brief Singleton accessor for a process-wide pool instance.
 *
 * Provides static convenience methods that forward to a single pool
 * whose lifetime is managed as a function-local static (Meyer's singleton).
 *
 * @par Thread safety
 * The underlying pool is created on the first call to instance() and is
 * guaranteed to be thread-safe in C++11 and later (magic statics). All
 * forwarded methods are as thread-safe as the corresponding pool methods.
 *
 * @par Pool size
 * The pool is created with @c std::thread::hardware_concurrency() threads.
 * This size is fixed for the lifetime of the process.
 *
 * @par Static destruction order
 * Because the pool is a function-local static, it is destroyed during static
 * destruction in reverse order of construction. Submitting work to the global
 * pool from destructors of other static objects is undefined behaviour if the
 * pool has already been destroyed.
 *
 * @par Copyability / movability
 * Not instantiable (private constructor). All access is through static
 * methods.
 *
 * @tparam PoolType The concrete pool type to wrap.
 */
template <typename PoolType>
class global_pool_backend
{
public:
  /**
   * @brief Pre-configure the number of threads before first use.
   *
   * Must be called before instance() is first invoked. Subsequent calls
   * are ignored (std::call_once semantics).
   */
  static void
  init(size_t num_threads)
  {
    std::call_once(init_flag(), [num_threads] { thread_count() = num_threads; });
  }

  /// @brief Access the singleton pool instance (created on first call).
  static auto
  instance() -> PoolType&
  {
    static PoolType pool(thread_count());
    return pool;
  }

  /// @name Forwarding wrappers
  /// All methods below simply forward to @c instance().method(...).
  /// @{

  template <typename F, typename... Args>
  static auto
  submit(F&& f, Args&&... args)
  {
    return instance().submit(std::forward<F>(f), std::forward<Args>(args)...);
  }

  template <typename F, typename... Args>
  static auto
  try_submit(F&& f, Args&&... args)
  {
    return instance().try_submit(std::forward<F>(f), std::forward<Args>(args)...);
  }

  template <typename F, typename... Args>
  static void
  post(F&& f, Args&&... args)
  {
    instance().post(std::forward<F>(f), std::forward<Args>(args)...);
  }

  template <typename F, typename... Args>
  static auto
  try_post(F&& f, Args&&... args)
  {
    return instance().try_post(std::forward<F>(f), std::forward<Args>(args)...);
  }

  template <typename Iterator>
  static auto
  submit_batch(Iterator begin, Iterator end)
  {
    return instance().submit_batch(begin, end);
  }

  template <typename Iterator>
  static auto
  try_submit_batch(Iterator begin, Iterator end)
  {
    return instance().try_submit_batch(begin, end);
  }

  template <typename Iterator, typename F>
  static void
  parallel_for_each(Iterator begin, Iterator end, F&& func)
  {
    instance().parallel_for_each(begin, end, std::forward<F>(func));
  }

  /// @}

private:
  global_pool_backend() = default;

  static auto
  init_flag() -> std::once_flag&
  {
    static std::once_flag flag;
    return flag;
  }

  static auto
  thread_count() -> size_t&
  {
    static size_t count = default_worker_count();
    return count;
  }
};

/**
 * @typedef global_thread_pool_backend
 * @brief Singleton accessor for the process-wide @c thread_pool_backend
 * instance.
 */
using global_thread_pool_backend = global_pool_backend<thread_pool_backend>;

/**
 * @typedef global_work_stealing_pool_backend
 * @brief Singleton accessor for the process-wide @ref
 * work_stealing_pool_backend instance.
 */
using global_work_stealing_pool_backend = global_pool_backend<work_stealing_pool_backend>;

/**
 * @brief Convenience wrapper that applies a callable to every element of a
 *        container in parallel using the @c global_thread_pool_backend
 * singleton.
 *
 * Equivalent to:
 * @code
 * global_thread_pool_backend::parallel_for_each(container.begin(),
 * container.end(), func);
 * @endcode
 *
 * The call blocks until every element has been processed.
 *
 * @tparam Container Any type exposing begin() / end() iterators.
 * @tparam F         Callable compatible with @c void(Container::value_type&).
 *
 * @param container The container whose elements will be processed.
 * @param func      The callable applied to each element.
 */
template <typename Container, typename F>
void
parallel_for_each(Container& container, F&& func)
{
  global_thread_pool_backend::parallel_for_each(container.begin(), container.end(), std::forward<F>(func));
}

} // namespace threadschedule::detail
