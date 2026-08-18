#pragma once

/**
 * @file native.hpp
 * @brief Scheduling policies, thread priority, and CPU affinity types.
 */

#include "../../expected.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#  include "../unique_handle.hpp"
#endif

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  if defined(__MINGW32__)
#    include <pthread.h>
#  endif
#else
#  include <pthread.h>
#  include <sched.h>
#  include <sys/resource.h>
#  include <sys/types.h>
#endif

namespace threadschedule::detail
{
// expected/result are provided by expected.hpp

#ifdef _WIN32
using native_thread_id = unsigned long; // DWORD thread id
#else
using native_thread_id = pid_t; // Linux TID via gettid()
#endif

/**
 * @brief Enumeration of available thread scheduling policies.
 *
 * Represents the OS-level scheduling policy applied to a thread. On Linux, the
 * enumerator values map directly to the POSIX `SCHED_*` constants defined in
 * `<sched.h>`. On Windows, they are stored as portable integer values and
 * translated to Windows-specific priority classes / scheduling behaviour at
 * the point of application.
 *
 * ### Linux behaviour
 * | Policy     | Description | Privileges required |
 * |------------|-----------------------------------------------------------------------------|------------------------------|
 * | OTHER      | Default CFS (Completely Fair Scheduler) time-sharing. | None
 * | | FIFO       | Real-time FIFO - runs until it yields or a higher-priority
 * thread arrives.  | `CAP_SYS_NICE` or root       | | RR | Real-time
 * round-robin - like FIFO but with a per-thread time quantum.       |
 * `CAP_SYS_NICE` or root       | | BATCH | Like OTHER but the scheduler
 * assumes the thread is CPU-bound (longer slices).| None | | IDLE | Extremely
 * low priority; runs only when no other runnable thread exists.      | None |
 * | DEADLINE   | EDF (Earliest Deadline First) real-time scheduling (Linux
 * >= 3.14).          | `CAP_SYS_NICE` or root |
 *
 * ### Windows behaviour
 * Windows does not expose POSIX scheduling policies. The library maps each
 * enumerator to an appropriate combination of process priority class and
 * thread priority level when applying the policy. FIFO and RR are both treated
 * as elevated real-time priorities; BATCH and IDLE are mapped to below-normal
 * and idle priority levels respectively.
 *
 * @note DEADLINE is only available on Linux when `SCHED_DEADLINE` is defined
 * by the kernel headers. It is not available on Windows.
 *
 * @warning Setting FIFO, RR, or DEADLINE without adequate privileges will fail
 *          with a permission error (`EPERM` on Linux).
 */
enum class native_scheduling_policy : std::uint_fast8_t
{
#ifdef _WIN32
  // Windows doesn't have the same scheduling policies as Linux
  // We'll use generic values
  other = 0, ///< Standard scheduling
  fifo = 1,  ///< First in, first out
  rr = 2,    ///< Round-robin
  batch = 3, ///< For batch style execution
  idle = 4   ///< For very low priority background tasks
#else
  other = SCHED_OTHER, ///< Standard round-robin time-sharing
  fifo = SCHED_FIFO,   ///< First in, first out
  rr = SCHED_RR,       ///< Round-robin
  batch = SCHED_BATCH, ///< For batch style execution
  idle = SCHED_IDLE,   ///< For very low priority background tasks
#  ifdef SCHED_DEADLINE
  deadline = SCHED_DEADLINE ///< Real-time deadline scheduling
#  endif
#endif
};

/**
 * @brief Value-semantic wrapper for a thread scheduling priority.
 *
 * Encapsulates a single integer priority in the range **[-20, 99]**. The value
 * is silently clamped to this range on construction (via `std::clamp`), so
 * out-of-range inputs never produce an invalid object.
 *
 * ### Semantics
 * For regular scheduling, lower numeric values denote **higher** scheduling
 * priority (following the Unix nice convention): -20 is the most favourable
 * and 19 is the least. For POSIX real-time policies (`FIFO` / `RR`), positive
 * values are treated as native real-time priorities where larger values mean
 * higher priority (typically 1..99 on Linux).
 *
 * ### Platform notes
 * - **Linux:** The value follows nice-level ordering for regular scheduling.
 *   For real-time policies, scheduler_parameters::create_for_policy() accepts
 *   native positive real-time priorities directly and maps non-positive
 *   nice-style values into the native POSIX priority range.
 * - **Windows:** The value is mapped to a Windows thread priority constant
 *   (e.g. `THREAD_PRIORITY_HIGHEST`, `THREAD_PRIORITY_LOWEST`) when applied.
 *
 * ### Type traits
 * - Trivially copyable and trivially movable.
 * - `constexpr`-constructible - can be used in compile-time contexts.
 * - All relational operators (`==`, `!=`, `<`, `<=`, `>`, `>=`) are provided
 *   and compare the underlying integer value.
 * - Not thread-safe: concurrent mutation of the same instance requires
 *   external synchronisation. Distinct instances may be used freely from
 *   different threads.
 *
 * @see scheduler_parameters::create_for_policy
 */
class native_thread_priority
{
public:
  constexpr explicit native_thread_priority(int priority = 0)
      : priority_(std::clamp(priority, min_priority, max_priority))
  {
  }

  [[nodiscard]] constexpr auto
  value() const noexcept -> int
  {
    return priority_;
  }
  [[nodiscard]] constexpr auto
  is_valid() const noexcept -> bool
  {
    return priority_ >= min_priority && priority_ <= max_priority;
  }

  [[nodiscard]] static constexpr auto
  lowest() noexcept -> native_thread_priority
  {
    return native_thread_priority(max_nice_priority);
  }
  [[nodiscard]] static constexpr auto
  normal() noexcept -> native_thread_priority
  {
    return native_thread_priority(0);
  }
  [[nodiscard]] static constexpr auto
  highest() noexcept -> native_thread_priority
  {
    return native_thread_priority(min_nice_priority);
  }
  [[nodiscard]] static constexpr auto
  realtime_lowest() noexcept -> native_thread_priority
  {
    return native_thread_priority(min_realtime_priority);
  }
  [[nodiscard]] static constexpr auto
  realtime_highest() noexcept -> native_thread_priority
  {
    return native_thread_priority(max_realtime_priority);
  }

  [[nodiscard]] constexpr auto
  operator==(native_thread_priority const& other) const noexcept -> bool
  {
    return priority_ == other.priority_;
  }
  [[nodiscard]] constexpr auto
  operator!=(native_thread_priority const& other) const noexcept -> bool
  {
    return priority_ != other.priority_;
  }
  [[nodiscard]] constexpr auto
  operator<(native_thread_priority const& other) const noexcept -> bool
  {
    return priority_ < other.priority_;
  }
  [[nodiscard]] constexpr auto
  operator<=(native_thread_priority const& other) const noexcept -> bool
  {
    return priority_ <= other.priority_;
  }
  [[nodiscard]] constexpr auto
  operator>(native_thread_priority const& other) const noexcept -> bool
  {
    return priority_ > other.priority_;
  }
  [[nodiscard]] constexpr auto
  operator>=(native_thread_priority const& other) const noexcept -> bool
  {
    return priority_ >= other.priority_;
  }

  [[nodiscard]] auto
  to_string() const -> std::string
  {
    std::ostringstream oss;
    oss << "native_thread_priority(" << priority_ << ")";
    return oss.str();
  }

private:
  static constexpr int min_nice_priority = -20;
  static constexpr int max_nice_priority = 19;
  static constexpr int min_realtime_priority = 1;
  static constexpr int max_realtime_priority = 99;
  static constexpr int min_priority = min_nice_priority;
  static constexpr int max_priority = max_realtime_priority;
  int priority_;
};

enum class native_scheduling_intent : std::uint_fast8_t
{
  background,
  normal,
  interactive,
  low_latency,
  realtime
};

enum class native_priority_model : std::uint_fast8_t
{
  intent,
  posix_nice,
  posix_realtime,
  windows_thread,
  platform_native
};

struct native_scheduling_config
{
  native_scheduling_intent intent{ native_scheduling_intent::normal };
  native_scheduling_policy policy{ native_scheduling_policy::other };
  native_thread_priority priority{ native_thread_priority::normal() };
  native_priority_model model{ native_priority_model::intent };
  bool valid{ true };
};

struct resolved_scheduling
{
  native_scheduling_policy policy{ native_scheduling_policy::other };
  native_thread_priority priority{ native_thread_priority::normal() };
  native_priority_model model{ native_priority_model::intent };
  bool valid{ true };
};

namespace native_schedule
{
[[nodiscard]] constexpr auto
background() noexcept -> native_scheduling_config
{
  return { native_scheduling_intent::background, native_scheduling_policy::idle, native_thread_priority::lowest(),
           native_priority_model::intent };
}

[[nodiscard]] constexpr auto
normal() noexcept -> native_scheduling_config
{
  return { native_scheduling_intent::normal, native_scheduling_policy::other, native_thread_priority::normal(),
           native_priority_model::posix_nice };
}

[[nodiscard]] constexpr auto
interactive() noexcept -> native_scheduling_config
{
  return { native_scheduling_intent::interactive, native_scheduling_policy::other, native_thread_priority{ -5 },
           native_priority_model::posix_nice };
}

[[nodiscard]] constexpr auto
low_latency() noexcept -> native_scheduling_config
{
  return { native_scheduling_intent::low_latency, native_scheduling_policy::other, native_thread_priority::highest(),
           native_priority_model::posix_nice };
}

[[nodiscard]] constexpr auto
realtime_fifo(int priority = 80) noexcept -> native_scheduling_config
{
  return { native_scheduling_intent::realtime, native_scheduling_policy::fifo, native_thread_priority{ priority },
           native_priority_model::posix_realtime };
}

[[nodiscard]] constexpr auto
realtime_rr(int priority = 80) noexcept -> native_scheduling_config
{
  return { native_scheduling_intent::realtime, native_scheduling_policy::rr, native_thread_priority{ priority },
           native_priority_model::posix_realtime };
}

[[nodiscard]] constexpr auto
posix_nice(int nice_value) noexcept -> native_scheduling_config
{
  return { native_scheduling_intent::normal, native_scheduling_policy::other, native_thread_priority{ nice_value },
           native_priority_model::posix_nice, nice_value >= -20 && nice_value <= 19 };
}

[[nodiscard]] constexpr auto
native(native_scheduling_policy policy, native_thread_priority priority) noexcept -> native_scheduling_config
{
  return { native_scheduling_intent::normal, policy, priority, native_priority_model::platform_native };
}

[[nodiscard]] constexpr auto
native_windows_priority(int priority) noexcept -> native_scheduling_config
{
#ifdef _WIN32
  bool const valid = priority == THREAD_PRIORITY_IDLE || priority == THREAD_PRIORITY_LOWEST
                     || priority == THREAD_PRIORITY_BELOW_NORMAL || priority == THREAD_PRIORITY_NORMAL
                     || priority == THREAD_PRIORITY_ABOVE_NORMAL || priority == THREAD_PRIORITY_HIGHEST
                     || priority == THREAD_PRIORITY_TIME_CRITICAL;
#else
  bool const valid = false;
#endif
  return { native_scheduling_intent::normal, native_scheduling_policy::other, native_thread_priority{ priority },
           native_priority_model::windows_thread, valid };
}
} // namespace native_schedule
constexpr auto
is_realtime_policy(native_scheduling_policy policy) noexcept -> bool
{
  return policy == native_scheduling_policy::fifo || policy == native_scheduling_policy::rr;
}

[[nodiscard]] constexpr auto
resolve_scheduling_config(native_scheduling_config const& config) noexcept -> resolved_scheduling
{
  if (config.model == native_priority_model::posix_realtime)
    {
      auto policy = is_realtime_policy(config.policy) ? config.policy : native_scheduling_policy::rr;
      return { policy, config.priority, config.model, config.valid };
    }

  if (config.model != native_priority_model::intent)
    return { config.policy, config.priority, config.model, config.valid };

  switch (config.intent)
    {
    case native_scheduling_intent::background:
      return { native_scheduling_policy::idle, native_thread_priority::lowest(), config.model, config.valid };
    case native_scheduling_intent::interactive:
      return { native_scheduling_policy::other, native_thread_priority{ -5 }, config.model, config.valid };
    case native_scheduling_intent::low_latency:
      return { native_scheduling_policy::other, native_thread_priority::highest(), config.model, config.valid };
    case native_scheduling_intent::realtime:
      return { is_realtime_policy(config.policy) ? config.policy : native_scheduling_policy::rr, config.priority,
               config.model, config.valid };
    case native_scheduling_intent::normal:
    default:
      return { native_scheduling_policy::other, native_thread_priority::normal(), config.model, config.valid };
    }
}

#ifdef _WIN32
inline auto
map_priority_to_win32(int prio_val) -> int
{
  if (prio_val <= -10)
    return THREAD_PRIORITY_HIGHEST;
  if (prio_val < 0)
    return THREAD_PRIORITY_ABOVE_NORMAL;
  if (prio_val == 0)
    return THREAD_PRIORITY_NORMAL;
  if (prio_val < 10)
    return THREAD_PRIORITY_BELOW_NORMAL;
  if (prio_val < 19)
    return THREAD_PRIORITY_LOWEST;
  return THREAD_PRIORITY_IDLE;
}
#endif
/**
 * @brief Manages a set of CPU indices to which a thread may be bound.
 *
 * native_thread_affinity is a value-semantic type that represents a CPU
 * affinity mask. It abstracts away the platform-specific details of
 * `cpu_set_t` (Linux) and processor-group bitmasks (Windows).
 *
 * ### Linux
 * Backed by a `cpu_set_t`. Supports CPU indices in the range
 * `[0, CPU_SETSIZE)` (typically 0-1023). The `native_handle()` accessor
 * provides a `const cpu_set_t&` for direct use with `pthread_setaffinity_np`
 * or `sched_setaffinity`.
 *
 * ### Windows
 * Backed by a 64-bit bitmask plus a processor group index (`WORD`). Windows
 * organises logical processors into groups of up to 64. This class supports
 * **a single group at a time**: the group is determined by the first CPU added
 * via `add_cpu()`. Subsequent calls to `add_cpu()` for CPUs that belong to a
 * different group are **silently ignored**. Use `get_group()` and `get_mask()`
 * to retrieve the platform-native values for `SetThreadGroupAffinity`.
 *
 * ### Thread safety
 * None. native_thread_affinity is a plain value type with no internal
 * synchronisation. Concurrent reads are safe; concurrent mutation (or a read
 * concurrent with a write) requires external locking.
 *
 * ### Copyability / movability
 * Implicitly copyable and movable (compiler-generated special members).
 *
 * @warning On Windows, CPUs from different processor groups cannot be combined
 *          in a single native_thread_affinity instance. If you need
 * cross-group affinity you must apply separate native_thread_affinity objects
 * per group.
 */
class native_thread_affinity
{
public:
  native_thread_affinity()
  {
#ifdef _WIN32
    group_ = 0;
    mask_ = 0;
#else
    CPU_ZERO(&cpuset_);
#endif
  }

  explicit native_thread_affinity(std::vector<int> const& cpus) : native_thread_affinity()
  {
    for (int cpu : cpus)
      {
        add_cpu(cpu);
      }
  }

  // Adds a CPU index. On Windows, indices >= 64 select group = cpu/64
  // automatically.
  void
  add_cpu(int cpu)
  {
#ifdef _WIN32
    if (cpu < 0)
      return;
    WORD g = static_cast<WORD>(cpu / 64);
    int bit = cpu % 64;
    if (!has_any())
      {
        group_ = g;
      }
    if (g != group_)
      {
        // Single-group affinity object: ignore CPUs from other groups
        return;
      }
    mask_ |= (static_cast<unsigned long long>(1) << bit);
#else
    if (cpu >= 0 && cpu < CPU_SETSIZE)
      {
        CPU_SET(cpu, &cpuset_);
      }
#endif
  }

  void
  remove_cpu(int cpu)
  {
#ifdef _WIN32
    if (cpu < 0)
      return;
    WORD g = static_cast<WORD>(cpu / 64);
    int bit = cpu % 64;
    if (g == group_)
      {
        mask_ &= ~(static_cast<unsigned long long>(1) << bit);
      }
#else
    if (cpu >= 0 && cpu < CPU_SETSIZE)
      {
        CPU_CLR(cpu, &cpuset_);
      }
#endif
  }

  [[nodiscard]] auto
  is_set(int cpu) const -> bool
  {
#ifdef _WIN32
    if (cpu < 0)
      return false;
    WORD g = static_cast<WORD>(cpu / 64);
    int bit = cpu % 64;
    return g == group_ && (mask_ & (static_cast<unsigned long long>(1) << bit)) != 0;
#else
    return cpu >= 0 && cpu < CPU_SETSIZE && CPU_ISSET(cpu, &cpuset_);
#endif
  }

  [[nodiscard]] auto
  has_cpu(int cpu) const -> bool
  {
    return is_set(cpu);
  }

  void
  clear()
  {
#ifdef _WIN32
    mask_ = 0;
#else
    CPU_ZERO(&cpuset_);
#endif
  }

  [[nodiscard]] auto
  get_cpus() const -> std::vector<int>
  {
    std::vector<int> cpus;
#ifdef _WIN32
    for (int i = 0; i < 64; ++i)
      {
        if (mask_ & (static_cast<unsigned long long>(1) << i))
          {
            cpus.push_back(static_cast<int>(group_) * 64 + i);
          }
      }
#else
    for (int i = 0; i < CPU_SETSIZE; ++i)
      {
        if (CPU_ISSET(i, &cpuset_))
          {
            cpus.push_back(i);
          }
      }
#endif
    return cpus;
  }

#ifdef _WIN32
  [[nodiscard]] unsigned long long
  get_mask() const
  {
    return mask_;
  }
  [[nodiscard]] WORD
  get_group() const
  {
    return group_;
  }
  [[nodiscard]] bool
  has_any() const
  {
    return mask_ != 0;
  }
#else
  [[nodiscard]] auto
  native_handle() const -> cpu_set_t const&
  {
    return cpuset_;
  }
#endif

  [[nodiscard]] auto
  to_string() const -> std::string
  {
    auto cpus = get_cpus();
    std::ostringstream oss;
    oss << "native_thread_affinity({";
    for (size_t i = 0; i < cpus.size(); ++i)
      {
        if (i > 0)
          oss << ", ";
        oss << cpus[i];
      }
    oss << "})";
    return oss.str();
  }

private:
#ifdef _WIN32
  WORD group_;
  unsigned long long mask_;
#else
  cpu_set_t cpuset_;
#endif
};

struct native_thread_config
{
  std::optional<std::string> name;
  std::optional<native_scheduling_config> scheduling;
  std::optional<native_thread_affinity> affinity;
};

/**
 * @brief Static utility class for constructing OS-native scheduling
 * parameters.
 *
 * scheduler_parameters translates the portable native_scheduling_policy and
 * native_thread_priority types into the platform-specific structures required
 * by the OS scheduling APIs (`sched_param` on Linux, a compatible POD on
 * Windows).
 *
 * ### `create_for_policy`
 * Builds a native scheduling-parameter structure for a given policy/priority
 * pair. The priority is **clamped** to the valid range for the requested
 * policy (queried at runtime on Linux via `sched_get_priority_min` /
 * `sched_get_priority_max`), so callers never need to pre-validate the range
 * themselves. Returns an @ref expected - on failure (e.g. an unrecognised
 * policy value) an `std::error_code` is returned instead.
 *
 * ### `get_priority_range`
 * Returns the width of the valid priority range (max - min) for a policy.
 * Useful for normalising priorities across policies.
 *
 * ### Platform differences
 * - **Linux:** Delegates to POSIX `sched_get_priority_min` /
 *   `sched_get_priority_max`. Positive priorities for real-time policies are
 *   used as native POSIX values; non-positive values are mapped from
 *   nice-style ordering into the native `sched_param` range.
 * - **Windows:** Returns a fixed range of 30. Normal priorities use the safe
 *   nice-to-Win32 mapping, while realtime policies select only
 *   `THREAD_PRIORITY_ABOVE_NORMAL` or `THREAD_PRIORITY_HIGHEST`. The result is
 *   stored in a lightweight `sched_param_win` POD.
 *
 * ### Thread safety
 * All members are static and stateless; concurrent calls from any number of
 * threads are safe.
 *
 * @note This class is not intended to be instantiated.
 *
 * @see native_scheduling_policy, native_thread_priority
 */
class scheduler_parameters
{
public:
#ifdef _WIN32
  // Windows doesn't use sched_param, but we'll define a compatible type
  struct sched_param_win
  {
    int sched_priority;
  };

  static expected<sched_param_win, std::error_code>
  create_for_policy(native_scheduling_policy policy, native_thread_priority priority)
  {
    sched_param_win param{};
    if (detail::is_realtime_policy(policy))
      {
        if (priority.value() >= 75 || priority.value() <= -10)
          param.sched_priority = THREAD_PRIORITY_HIGHEST;
        else
          param.sched_priority = THREAD_PRIORITY_ABOVE_NORMAL;
        return param;
      }

    param.sched_priority = detail::map_priority_to_win32(priority.value());
    return param;
  }

  static expected<int, std::error_code>
  get_priority_range([[maybe_unused]] native_scheduling_policy policy)
  {
    // Windows thread priorities range from -15 to +15
    return 30;
  }
#else
  static auto
  create_for_policy(native_scheduling_policy policy, native_thread_priority priority)
      -> expected<sched_param, std::error_code>
  {
    sched_param param{};

    int const policy_int = static_cast<int>(policy);
    int const min_prio = sched_get_priority_min(policy_int);
    int const max_prio = sched_get_priority_max(policy_int);

    if (min_prio == -1 || max_prio == -1)
      {
        return unexpected(std::make_error_code(std::errc::invalid_argument));
      }

    if (min_prio == max_prio)
      {
        param.sched_priority = min_prio;
        return param;
      }

    if (detail::is_realtime_policy(policy) && priority.value() > 0)
      {
        param.sched_priority = std::clamp(priority.value(), min_prio, max_prio);
        return param;
      }

    constexpr int highest_nice_value = -20;
    constexpr int lowest_nice_value = 19;
    constexpr int user_span = lowest_nice_value - highest_nice_value;
    int const native_span = max_prio - min_prio;
    int const offset = std::clamp(priority.value(), highest_nice_value, lowest_nice_value) - highest_nice_value;
    param.sched_priority = max_prio - (((offset * native_span) + (user_span / 2)) / user_span);
    return param;
  }

  static auto
  get_priority_range(native_scheduling_policy policy) -> expected<int, std::error_code>
  {
    int const policy_int = static_cast<int>(policy);
    int const min_prio = sched_get_priority_min(policy_int);
    int const max_prio = sched_get_priority_max(policy_int);

    if (min_prio == -1 || max_prio == -1)
      {
        return unexpected(std::make_error_code(std::errc::invalid_argument));
      }

    return max_prio - min_prio;
  }
#endif
};

/**
 * @brief String conversion utilities
 */
inline auto
to_string(native_scheduling_policy policy) -> std::string
{
  switch (policy)
    {
    case native_scheduling_policy::other:
      return "OTHER";
    case native_scheduling_policy::fifo:
      return "FIFO";
    case native_scheduling_policy::rr:
      return "RR";
    case native_scheduling_policy::batch:
      return "BATCH";
    case native_scheduling_policy::idle:
      return "IDLE";
#if defined(SCHED_DEADLINE) && !defined(_WIN32)
    case native_scheduling_policy::deadline:
      return "DEADLINE";
#endif
    default:
      return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// detail:: free functions for thread configuration (priority, policy,
// affinity)
//
// Overloaded by handle type so that every wrapper class can delegate with a
// single call: detail::apply_priority(handle, priority).
// ---------------------------------------------------------------------------

#ifdef _WIN32
#  include "windows.hpp"
#else
#  include "posix.hpp"
#endif

template <typename NativeHandle>
inline auto
apply_affinity_checked(NativeHandle handle, native_thread_affinity const& affinity) -> expected<void, std::error_code>
{
  auto previous = read_affinity(handle);
  if (!previous)
    return unexpected(previous.error());

  auto applied = apply_affinity(handle, affinity);
  if (!applied)
    return applied;

  auto effective = read_affinity(handle);
  if (effective && effective->get_cpus() == affinity.get_cpus())
    return {};

  auto restored = apply_affinity(handle, previous.value());
  if (!restored)
    return unexpected(std::make_error_code(std::errc::state_not_recoverable));
  if (!effective)
    return unexpected(effective.error());
  return unexpected(std::make_error_code(std::errc::invalid_argument));
}

template <typename NativeHandle>
inline auto
apply_scheduling_config(NativeHandle handle, native_thread_id tid, native_scheduling_config const& config)
    -> expected<void, std::error_code>
{
  auto const scheduling = resolve_scheduling_config(config);
  if (!scheduling.valid)
    return unexpected(std::make_error_code(std::errc::invalid_argument));

#ifdef _WIN32
  if (scheduling.model == native_priority_model::windows_thread)
    return apply_windows_thread_priority(handle, scheduling.priority.value());
#endif

  if (scheduling.model == native_priority_model::posix_nice)
    {
#ifdef _WIN32
      (void)tid;
      return apply_nice_value(handle, scheduling.priority.value());
#else
      if (tid <= 0)
        return unexpected(std::make_error_code(std::errc::operation_not_supported));
      auto policy_result
          = apply_scheduling_policy(handle, native_scheduling_policy::other, native_thread_priority::normal());
      if (!policy_result)
        return policy_result;
      return apply_nice_value(tid, scheduling.priority.value());
#endif
    }

  return apply_scheduling_policy(handle, scheduling.policy, scheduling.priority);
}

template <typename NativeHandle>
inline auto
read_effective_nice(NativeHandle handle, native_thread_id tid) -> expected<int, std::error_code>
{
#ifdef _WIN32
  (void)tid;
  return read_nice_value(handle);
#else
  if (tid <= 0)
    return unexpected(std::make_error_code(std::errc::operation_not_supported));
  auto const policy = read_scheduling_policy(handle);
  if (!policy.has_value())
    return unexpected(std::make_error_code(std::errc::no_such_process));
  if (policy.value() == native_scheduling_policy::idle)
    return 19;
  if (is_realtime_policy(policy.value()))
    return unexpected(std::make_error_code(std::errc::operation_not_supported));
  return read_nice_value(tid);
#endif
}

} // namespace threadschedule::detail
