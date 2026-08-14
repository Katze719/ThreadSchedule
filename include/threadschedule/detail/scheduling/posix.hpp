#pragma once

/**
 * @file detail/scheduling/posix.hpp
 * @brief POSIX and Linux native thread-control implementation fragment.
 *
 * Included by native.hpp inside threadschedule::detail after the shared native
 * scheduling value types have been declared.
 */

// --- shared implementation for pthread_t and pid_t scheduling ---

inline auto
pthread_call_result(int result) -> expected<void, std::error_code>
{
  if (result == 0)
    return {};
  return unexpected(std::error_code(result, std::generic_category()));
}

template <typename SetSchedFn>
inline auto
apply_sched_params(native_scheduling_policy policy, native_thread_priority priority, SetSchedFn&& set_sched)
    -> expected<void, std::error_code>
{
  int const policy_int = static_cast<int>(policy);
  auto params_result = scheduler_parameters::create_for_policy(policy, priority);
  if (!params_result.has_value())
    return unexpected(params_result.error());
  if (set_sched(policy_int, &params_result.value()) == 0)
    return {};
  return unexpected(std::error_code(errno, std::generic_category()));
}

// --- pthread_t overloads used by std::thread::native_handle() ---

inline auto
apply_scheduling_policy(pthread_t handle, native_scheduling_policy policy, native_thread_priority priority)
    -> expected<void, std::error_code>
{
  int const policy_int = static_cast<int>(policy);
  auto params_result = scheduler_parameters::create_for_policy(policy, priority);
  if (!params_result.has_value())
    return unexpected(params_result.error());
  int const result = pthread_setschedparam(handle, policy_int, &params_result.value());
  return pthread_call_result(result);
}

inline auto
apply_priority(pthread_t handle, native_thread_priority priority) -> expected<void, std::error_code>
{
  return apply_scheduling_policy(handle, native_scheduling_policy::other, priority);
}

inline auto
apply_affinity(pthread_t handle, native_thread_affinity const& affinity) -> expected<void, std::error_code>
{
  int const result = pthread_setaffinity_np(handle, sizeof(cpu_set_t), &affinity.native_handle());
  return pthread_call_result(result);
}

inline auto
apply_name(pthread_t handle, std::string const& name) -> expected<void, std::error_code>
{
  if (name.length() > 15)
    return unexpected(std::make_error_code(std::errc::invalid_argument));
  int const result = pthread_setname_np(handle, name.c_str());
  if (result == 0)
    return {};
  return unexpected(std::error_code(result, std::generic_category()));
}

inline auto
read_name(pthread_t handle) -> expected<std::string, std::error_code>
{
  char name[16];
  int const result = pthread_getname_np(handle, name, sizeof(name));
  if (result == 0)
    return std::string(name);
  return unexpected(std::error_code(result, std::generic_category()));
}

inline auto
read_affinity(pthread_t handle) -> expected<native_thread_affinity, std::error_code>
{
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  int const result = pthread_getaffinity_np(handle, sizeof(cpu_set_t), &cpuset);
  if (result != 0)
    return unexpected(std::error_code(result, std::generic_category()));

  std::vector<int> cpus;
  for (int i = 0; i < CPU_SETSIZE; ++i)
    {
      if (CPU_ISSET(i, &cpuset))
        cpus.push_back(i);
    }
  return native_thread_affinity(cpus);
}

inline auto
read_priority(pthread_t handle) -> std::optional<int>
{
  int policy = 0;
  sched_param param{};
  if (pthread_getschedparam(handle, &policy, &param) == 0)
    return param.sched_priority;
  return std::nullopt;
}

inline auto
read_scheduling_policy(pthread_t handle) -> std::optional<native_scheduling_policy>
{
  int policy = 0;
  sched_param param{};
  if (pthread_getschedparam(handle, &policy, &param) == 0)
    return static_cast<native_scheduling_policy>(policy);
  return std::nullopt;
}

// --- pid_t / TID overloads (thread_by_name_view) ---

inline auto
apply_scheduling_policy(pid_t tid, native_scheduling_policy policy, native_thread_priority priority)
    -> expected<void, std::error_code>
{
  return apply_sched_params(policy, priority, [tid](int p, sched_param* sp) { return sched_setscheduler(tid, p, sp); });
}

inline auto
apply_priority(pid_t tid, native_thread_priority priority) -> expected<void, std::error_code>
{
  return apply_scheduling_policy(tid, native_scheduling_policy::other, priority);
}

inline auto
apply_nice_value(pid_t tid, int nice_value) -> expected<void, std::error_code>
{
  if (nice_value < -20 || nice_value > 19)
    return unexpected(std::make_error_code(std::errc::invalid_argument));
  if (tid <= 0)
    return unexpected(std::make_error_code(std::errc::no_such_process));
  if (setpriority(PRIO_PROCESS, static_cast<id_t>(tid), nice_value) == 0)
    return {};
  return unexpected(std::error_code(errno, std::generic_category()));
}

inline auto
apply_affinity(pid_t tid, native_thread_affinity const& affinity) -> expected<void, std::error_code>
{
  if (sched_setaffinity(tid, sizeof(cpu_set_t), &affinity.native_handle()) == 0)
    return {};
  return unexpected(std::error_code(errno, std::generic_category()));
}

inline auto
apply_name(pid_t tid, std::string const& name) -> expected<void, std::error_code>
{
  if (name.length() > 15)
    return unexpected(std::make_error_code(std::errc::invalid_argument));

  std::string const path = std::string("/proc/self/task/") + std::to_string(tid) + "/comm";
  std::ofstream out(path);
  if (!out)
    return unexpected(std::error_code(errno, std::generic_category()));

  out << name;
  out.flush();
  if (!out)
    return unexpected(std::error_code(errno, std::generic_category()));
  return {};
}

inline auto
read_name(pid_t tid) -> expected<std::string, std::error_code>
{
  std::string const path = std::string("/proc/self/task/") + std::to_string(tid) + "/comm";
  std::ifstream in(path);
  if (!in)
    return unexpected(std::error_code(errno, std::generic_category()));

  std::string current;
  std::getline(in, current);
  if (in.bad())
    return unexpected(std::error_code(errno, std::generic_category()));
  if (!current.empty() && current.back() == '\n')
    current.pop_back();
  return current;
}

inline auto
read_affinity(pid_t tid) -> expected<native_thread_affinity, std::error_code>
{
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  if (sched_getaffinity(tid, sizeof(cpu_set_t), &cpuset) != 0)
    return unexpected(std::error_code(errno, std::generic_category()));

  std::vector<int> cpus;
  for (int i = 0; i < CPU_SETSIZE; ++i)
    {
      if (CPU_ISSET(i, &cpuset))
        cpus.push_back(i);
    }
  return native_thread_affinity(cpus);
}

inline auto
read_priority(pid_t tid) -> std::optional<int>
{
  sched_param param{};
  if (sched_getparam(tid, &param) == 0)
    return param.sched_priority;
  return std::nullopt;
}

inline auto
read_nice_value(pid_t tid) -> expected<int, std::error_code>
{
  if (tid <= 0)
    return unexpected(std::make_error_code(std::errc::no_such_process));
  errno = 0;
  int const value = getpriority(PRIO_PROCESS, static_cast<id_t>(tid));
  if (errno == 0)
    return value;
  return unexpected(std::error_code(errno, std::generic_category()));
}

inline auto
read_scheduling_policy(pid_t tid) -> std::optional<native_scheduling_policy>
{
  int const policy = sched_getscheduler(tid);
  if (policy == -1)
    return std::nullopt;
  return static_cast<native_scheduling_policy>(policy);
}
