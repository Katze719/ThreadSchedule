#pragma once

/** @file detail/registry/thread_control_block.hpp
 *  @brief Native thread metadata and lifecycle-safe control blocks.
 */

#include "../../expected.hpp"
#include "../callable/copyable_function.hpp"
#include "../scheduling/native.hpp"
#include "../thread/identity.hpp"
#include "../thread_backend.hpp"

#ifdef _WIN32
#  include "../unique_handle.hpp"
#endif

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace threadschedule::detail
{

class thread_registry_backend;
class registration_guard_backend;

/**
 * @brief Snapshot of metadata for a single registered thread.
 *
 * This is a POD-like value type that captures thread identity, lifecycle
 * state, and an optional handle to the underlying thread_control_block.
 * Instances are returned by thread_registry_backend queries and are safe to
 * store, copy, and inspect from any thread.
 *
 * @par Thread safety
 * Instances are plain value types and carry no internal synchronisation.
 * Concurrent reads are safe; concurrent read/write on the *same* instance is
 * not.  The @c control shared_ptr is ref-counted and the pointee
 * (@ref thread_control_block) is itself thread-safe.
 *
 * @par Copyability / movability
 * Fully copyable and movable (regular value semantics).
 *
 * @par Lifetime
 * A registered_thread_info_backend is a *snapshot* - it may outlive the thread
 * it describes.  The @c alive flag reflects the state at the time the snapshot
 * was taken; it is **not** updated retroactively when the thread unregisters.
 *
 * @par Fields
 * - @c tid   - OS-level thread identifier (@c pid_t on Linux via
 *               @c gettid(), @c DWORD on Windows).
 * - @c std_id - The corresponding @c std::thread::id.
 * - @c name  - Human-readable name given at registration time.
 * - @c component - Optional logical grouping tag (e.g. "io", "compute").
 * - @c alive - @c true while the thread is registered; set to @c false when
 *               the thread calls @c unregister_current_thread().
 * - @c control - Shared pointer to the thread's @ref thread_control_block. May
 * be
 *                 @c nullptr if the thread was registered without a control
 *                 block (i.e. via the name-only overload of
 *                 @c register_current_thread()).
 */
struct registered_thread_info_backend
{
  native_thread_id tid{};
  std::thread::id std_id;
  std::string name;
  std::string component;
  bool alive{ true };
  std::shared_ptr<class thread_control_block> control;
};

using registry_callback = detail::copyable_function<void(registered_thread_info_backend const&)>;

/**
 * @brief Per-thread control handle for OS-level scheduling operations.
 *
 * A thread_control_block captures the native thread handle (pthread_t on
 * Linux, a duplicated @c HANDLE on Windows) at construction time and exposes
 * cross-platform methods to modify the thread's affinity, priority,
 * scheduling policy, and OS-visible name.
 *
 * @par Creation
 * Always use the static factory create_for_current_thread().  It **must** be
 * called from the thread it will represent, because it snapshots
 * @c pthread_self() / @c GetCurrentThread().
 *
 * @par Ownership
 * thread_control_block is intended to be held via @c std::shared_ptr so that
 * the registry, the owning thread, and any observers can all share the same
 * instance.  The static factory already returns a @c shared_ptr.
 *
 * @par Thread safety
 * - The object is **not** copyable and **not** movable (identity type).
 * - All @c set_* methods are safe to call from **any** thread - they operate
 *   on the stored native handle, not on thread-local state.
 * - Concurrent calls to different @c set_* methods on the same instance are
 *   safe (each call is a single OS syscall on the stored handle).
 * - The control block is invalidated automatically while its represented
 *   thread exits. Later operations return @c std::errc::no_such_process.
 *
 * @par Platform notes
 * - **Linux**: stores @c pthread_t obtained via @c pthread_self().  No
 *   resource is owned; the handle is valid for the lifetime of the thread.
 * - **Windows**: duplicates the pseudo-handle returned by
 *   @c GetCurrentThread() into a real @c HANDLE with
 *   @c THREAD_SET_INFORMATION | @c THREAD_QUERY_INFORMATION rights.  The
 *   duplicated handle is closed in the destructor.
 *
 * @par Caveats
 * - Do **not** construct directly; always use create_for_current_thread().
 * - On Linux, @c set_name() enforces the 15-character POSIX limit and
 *   returns @c std::errc::invalid_argument if exceeded.
 */
class thread_control_block
{
public:
  thread_control_block() = default;
  thread_control_block(thread_control_block const&) = delete;
  auto operator=(thread_control_block const&) -> thread_control_block& = delete;
  thread_control_block(thread_control_block&&) = delete;
  auto operator=(thread_control_block&&) -> thread_control_block& = delete;

  ~thread_control_block() = default;

  [[nodiscard]] auto
  tid() const noexcept -> native_thread_id
  {
    return tid_;
  }
  [[nodiscard]] auto
  std_id() const noexcept -> std::thread::id
  {
    return std_id_;
  }

private:
  [[nodiscard]] auto
  native_handle() const
  {
#ifdef _WIN32
    return handle_.get();
#else
    return pthread_handle_;
#endif
  }

public:
  [[nodiscard]] auto
  set_affinity(native_thread_affinity const& affinity) const -> expected<void, std::error_code>
  {
    std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
    if (!target_is_active())
      return inactive_error();
#ifdef _WIN32
    return detail::apply_affinity_checked(native_handle(), affinity);
#else
    return detail::apply_affinity_checked(tid_, affinity);
#endif
  }

  [[nodiscard]] auto
  set_priority(native_thread_priority priority) const -> expected<void, std::error_code>
  {
    std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
    if (!target_is_active())
      return inactive_error();
#ifdef _WIN32
    return detail::apply_priority(native_handle(), priority);
#else
    return detail::apply_priority(tid_, priority);
#endif
  }

  [[nodiscard]] auto
  set_nice_value(int nice_value) const -> expected<void, std::error_code>
  {
    std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
    if (!target_is_active())
      return inactive_error();
#ifdef _WIN32
    return detail::apply_nice_value(native_handle(), nice_value);
#else
    return detail::apply_nice_value(tid_, nice_value);
#endif
  }

  [[nodiscard]] auto
  get_nice_value() const -> expected<int, std::error_code>
  {
    std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
    if (!target_is_active())
      return inactive_error();
#ifdef _WIN32
    return detail::read_effective_nice(native_handle(), tid_);
#else
    return detail::read_effective_nice(tid_, tid_);
#endif
  }

  [[nodiscard]] auto
  set_scheduling_policy(native_scheduling_policy policy, native_thread_priority priority) const
      -> expected<void, std::error_code>
  {
    std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
    if (!target_is_active())
      return inactive_error();
#ifdef _WIN32
    return detail::apply_scheduling_policy(native_handle(), policy, priority);
#else
    return detail::apply_scheduling_policy(tid_, policy, priority);
#endif
  }

  [[nodiscard]] auto
  configure(native_scheduling_config const& config) const -> expected<void, std::error_code>
  {
    std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
    if (!target_is_active())
      return inactive_error();
#ifdef _WIN32
    return detail::apply_scheduling_config(native_handle(), tid_, config);
#else
    return detail::apply_scheduling_config(tid_, tid_, config);
#endif
  }

  [[nodiscard]] auto
  set_name(std::string const& name) const -> expected<void, std::error_code>
  {
    std::shared_lock<std::shared_mutex> lock(lifecycle_mutex_);
    if (!target_is_active())
      return inactive_error();
#ifdef _WIN32
    return detail::apply_name(native_handle(), name);
#else
    return detail::apply_name(tid_, name);
#endif
  }

  static auto
  create_for_current_thread() -> std::shared_ptr<thread_control_block>
  {
    auto block = std::make_shared<thread_control_block>();
    block->tid_ = thread_info::get_thread_id();
    block->std_id_ = std::this_thread::get_id();
#ifdef _WIN32
    HANDLE realHandle = nullptr;
    if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &realHandle,
                        THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE, 0)
        == 0)
      throw std::system_error(detail::last_win32_error(), "DuplicateHandle");
    block->handle_.reset(realHandle);
#else
    block->pthread_handle_ = pthread_self();
    block->start_time_ = read_thread_start_time(block->tid_);
#endif
    current_thread_controls().add(block);
    return block;
  }

private:
  struct thread_exit_controls
  {
    void
    add(std::shared_ptr<thread_control_block> const& control)
    {
      controls_.erase(
          std::remove_if(controls_.begin(), controls_.end(), [](auto const& item) { return item.expired(); }),
          controls_.end());
      controls_.push_back(control);
    }

    ~thread_exit_controls()
    {
      for (auto const& item : controls_)
        if (auto control = item.lock())
          control->deactivate();
    }

    std::vector<std::weak_ptr<thread_control_block>> controls_;
  };

  [[nodiscard]] static auto
  current_thread_controls() -> thread_exit_controls&
  {
    thread_local thread_exit_controls controls;
    return controls;
  }

  [[nodiscard]] static auto
  inactive_error() -> unexpected<std::error_code>
  {
    return unexpected(std::make_error_code(std::errc::no_such_process));
  }

  [[nodiscard]] auto
  target_is_active() const noexcept -> bool
  {
    if (!active_)
      return false;
#ifdef _WIN32
    return static_cast<bool>(handle_);
#else
    // Without a generation value a recycled TID cannot be distinguished
    // safely from the original target. Fail closed instead of risking that
    // native controls are applied to an unrelated thread.
    if (!start_time_.has_value())
      return false;
    auto const current = read_thread_start_time(tid_);
    return current.has_value() && current == start_time_;
#endif
  }

  void
  deactivate() noexcept
  {
    std::unique_lock<std::shared_mutex> lock(lifecycle_mutex_);
    active_ = false;
  }

  friend class thread_registry_backend;

  mutable std::shared_mutex lifecycle_mutex_;
  bool active_{ true };
  native_thread_id tid_{};
  std::thread::id std_id_;
#ifdef _WIN32
  detail::unique_handle handle_;
#else
  pthread_t pthread_handle_{};
  std::optional<std::uint64_t> start_time_;
#endif
};

} // namespace threadschedule::detail
