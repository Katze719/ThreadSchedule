#pragma once

/**
 * @file detail/scheduling/windows.hpp
 * @brief Windows and MinGW native thread-control implementation fragment.
 *
 * Included by native.hpp inside threadschedule::detail after the shared native
 * scheduling value types have been declared.
 */

inline auto last_win32_error() -> std::error_code;

inline auto
apply_priority(HANDLE handle, native_thread_priority priority) -> expected<void, std::error_code>
{
  if (!handle)
    return unexpected(std::make_error_code(std::errc::no_such_process));
  if (SetThreadPriority(handle, map_priority_to_win32(priority.value())) != 0)
    return {};
  return unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
}

inline auto
apply_windows_thread_priority(HANDLE handle, int priority) -> expected<void, std::error_code>
{
  if (!handle)
    return unexpected(std::make_error_code(std::errc::no_such_process));
  if (SetThreadPriority(handle, priority) != 0)
    return {};
  return unexpected(last_win32_error());
}

inline auto
apply_nice_value(HANDLE handle, int nice_value) -> expected<void, std::error_code>
{
  if (nice_value < -20 || nice_value > 19)
    return unexpected(std::make_error_code(std::errc::invalid_argument));
  return apply_priority(handle, native_thread_priority{ nice_value });
}

inline auto
apply_scheduling_policy(HANDLE handle, native_scheduling_policy policy, native_thread_priority priority)
    -> expected<void, std::error_code>
{
  if (!handle)
    return unexpected(std::make_error_code(std::errc::no_such_process));
  auto params_result = scheduler_parameters::create_for_policy(policy, priority);
  if (!params_result.has_value())
    return unexpected(params_result.error());
  if (SetThreadPriority(handle, params_result.value().sched_priority) != 0)
    return {};
  return unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
}

inline auto
apply_affinity(HANDLE handle, native_thread_affinity const& affinity) -> expected<void, std::error_code>
{
  if (!handle)
    return unexpected(std::make_error_code(std::errc::no_such_process));
  if (!affinity.has_any())
    return unexpected(std::make_error_code(std::errc::invalid_argument));
  using set_thread_group_affinity_fn = BOOL(WINAPI*)(HANDLE, const GROUP_AFFINITY*, PGROUP_AFFINITY);
  HMODULE module = GetModuleHandleW(L"kernel32.dll");
  if (module)
    {
      auto set_group_affinity = reinterpret_cast<set_thread_group_affinity_fn>(
          reinterpret_cast<void*>(GetProcAddress(module, "SetThreadGroupAffinity")));
      if (set_group_affinity)
        {
          GROUP_AFFINITY ga{};
          ga.Mask = static_cast<KAFFINITY>(affinity.get_mask());
          ga.Group = affinity.get_group();
          if (set_group_affinity(handle, &ga, nullptr) != 0)
            return {};
          return unexpected(last_win32_error());
        }
    }
  if (affinity.get_group() != 0)
    return unexpected(std::make_error_code(std::errc::function_not_supported));
  DWORD_PTR mask = static_cast<DWORD_PTR>(affinity.get_mask());
  if (SetThreadAffinityMask(handle, mask) != 0)
    return {};
  return unexpected(last_win32_error());
}

using set_thread_description_fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
using get_thread_description_fn = HRESULT(WINAPI*)(HANDLE, PWSTR*);

class hresult_error_category final : public std::error_category
{
public:
  [[nodiscard]] auto
  name() const noexcept -> char const* override
  {
    return "HRESULT";
  }

  [[nodiscard]] auto
  message(int value) const -> std::string override
  {
    std::ostringstream stream;
    stream << "HRESULT 0x" << std::hex << static_cast<std::uint32_t>(value);
    return stream.str();
  }
};

inline auto
hresult_category() -> std::error_category const&
{
  static hresult_error_category const category;
  return category;
}

inline auto
error_from_hresult(HRESULT result) -> std::error_code
{
  if (HRESULT_FACILITY(result) == FACILITY_WIN32)
    return { static_cast<int>(HRESULT_CODE(result)), std::system_category() };
  return { static_cast<int>(result), hresult_category() };
}

inline auto
last_win32_error() -> std::error_code
{
  return { static_cast<int>(GetLastError()), std::system_category() };
}

struct thread_description_api
{
  set_thread_description_fn set = nullptr;
  get_thread_description_fn get = nullptr;
  bool found_module = false;
  std::error_code lookup_error{};
};

using module_lookup_fn = HMODULE(WINAPI*)(LPCWSTR);
using proc_lookup_fn = FARPROC(WINAPI*)(HMODULE, LPCSTR);

inline auto
resolve_thread_description_api(module_lookup_fn module_lookup = GetModuleHandleW,
                               proc_lookup_fn proc_lookup = GetProcAddress) -> thread_description_api
{
  thread_description_api result;
  DWORD last_error = ERROR_SUCCESS;
  constexpr wchar_t const* modules[] = { L"kernel32.dll", L"kernelbase.dll" };
  for (auto const* module_name : modules)
    {
      SetLastError(ERROR_SUCCESS);
      HMODULE const module = module_lookup(module_name);
      if (!module)
        {
          DWORD const error = GetLastError();
          if (error != ERROR_SUCCESS)
            last_error = error;
          continue;
        }
      result.found_module = true;
      if (!result.set)
        result.set = reinterpret_cast<set_thread_description_fn>(
            reinterpret_cast<void*>(proc_lookup(module, "SetThreadDescription")));
      if (!result.get)
        result.get = reinterpret_cast<get_thread_description_fn>(
            reinterpret_cast<void*>(proc_lookup(module, "GetThreadDescription")));
    }
  if (!result.found_module)
    result.lookup_error
        = { static_cast<int>(last_error == ERROR_SUCCESS ? ERROR_MOD_NOT_FOUND : last_error), std::system_category() };
  return result;
}

inline auto
resolved_set_thread_description() -> expected<set_thread_description_fn, std::error_code>
{
  static std::atomic<set_thread_description_fn> cached{ nullptr };
  if (auto const function = cached.load(std::memory_order_acquire))
    return function;
  auto const resolved = resolve_thread_description_api();
  if (!resolved.set)
    {
      std::error_code const error
          = resolved.found_module ? std::make_error_code(std::errc::function_not_supported) : resolved.lookup_error;
      return unexpected(error);
    }
  set_thread_description_fn expected_null = nullptr;
  cached.compare_exchange_strong(expected_null, resolved.set, std::memory_order_release, std::memory_order_acquire);
  return expected_null ? expected_null : resolved.set;
}

inline auto
resolved_get_thread_description() -> expected<get_thread_description_fn, std::error_code>
{
  static std::atomic<get_thread_description_fn> cached{ nullptr };
  if (auto const function = cached.load(std::memory_order_acquire))
    return function;
  auto const resolved = resolve_thread_description_api();
  if (!resolved.get)
    {
      std::error_code const error
          = resolved.found_module ? std::make_error_code(std::errc::function_not_supported) : resolved.lookup_error;
      return unexpected(error);
    }
  get_thread_description_fn expected_null = nullptr;
  cached.compare_exchange_strong(expected_null, resolved.get, std::memory_order_release, std::memory_order_acquire);
  return expected_null ? expected_null : resolved.get;
}

inline auto
utf8_to_utf16(std::string const& value) -> expected<std::wstring, std::error_code>
{
  if (value.empty())
    return std::wstring{};
  int const size
      = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (size == 0)
    return unexpected(last_win32_error());
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  int const written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                          result.data(), size);
  if (written == 0)
    return unexpected(last_win32_error());
  result.resize(static_cast<std::size_t>(written));
  return result;
}

struct local_free_deleter
{
  void
  operator()(wchar_t* value) const noexcept
  {
    if (value)
      LocalFree(value);
  }
};

inline auto
utf16_to_utf8(PCWSTR value) -> expected<std::string, std::error_code>
{
  int const size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (size == 0)
    return unexpected(last_win32_error());
  std::string result(static_cast<std::size_t>(size), '\0');
  int const written = WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
  if (written == 0)
    return unexpected(last_win32_error());
  result.resize(static_cast<std::size_t>(written - 1));
  return result;
}

inline auto
apply_name(HANDLE handle, std::string const& name) -> expected<void, std::error_code>
{
  if (!handle)
    return unexpected(std::make_error_code(std::errc::no_such_process));
  auto const wide = utf8_to_utf16(name);
  if (!wide.has_value())
    return unexpected(wide.error());
  auto const set_description = resolved_set_thread_description();
  if (!set_description.has_value())
    return unexpected(set_description.error());
  HRESULT const result = set_description.value()(handle, wide.value().c_str());
  if (SUCCEEDED(result))
    return {};
  return unexpected(error_from_hresult(result));
}

inline auto
read_name(HANDLE handle) -> expected<std::string, std::error_code>
{
  if (!handle)
    return unexpected(std::make_error_code(std::errc::no_such_process));
  auto const get_description = resolved_get_thread_description();
  if (!get_description.has_value())
    return unexpected(get_description.error());
  PWSTR raw_name = nullptr;
  HRESULT const result = get_description.value()(handle, &raw_name);
  std::unique_ptr<wchar_t, local_free_deleter> name(raw_name);
  if (FAILED(result))
    return unexpected(error_from_hresult(result));
  if (!name)
    return unexpected(std::make_error_code(std::errc::io_error));
  return utf16_to_utf8(name.get());
}

inline auto
read_affinity(HANDLE handle) -> expected<native_thread_affinity, std::error_code>
{
  if (!handle)
    return unexpected(std::make_error_code(std::errc::no_such_process));
  using get_thread_group_affinity_fn = BOOL(WINAPI*)(HANDLE, PGROUP_AFFINITY);
  HMODULE module = GetModuleHandleW(L"kernel32.dll");
  if (!module)
    return unexpected(last_win32_error());
  auto get_group_affinity = reinterpret_cast<get_thread_group_affinity_fn>(
      reinterpret_cast<void*>(GetProcAddress(module, "GetThreadGroupAffinity")));
  if (!get_group_affinity)
    return unexpected(std::make_error_code(std::errc::function_not_supported));
  GROUP_AFFINITY ga{};
  if (get_group_affinity(handle, &ga) == 0)
    return unexpected(last_win32_error());

  native_thread_affinity affinity;
  for (int i = 0; i < 64; ++i)
    {
      if ((ga.Mask & (static_cast<KAFFINITY>(1) << i)) != 0)
        affinity.add_cpu(static_cast<int>(ga.Group) * 64 + i);
    }
  return affinity;
}

inline auto
read_priority(HANDLE handle) -> std::optional<int>
{
  if (!handle)
    return std::nullopt;
  int const priority = GetThreadPriority(handle);
  if (priority == THREAD_PRIORITY_ERROR_RETURN)
    return std::nullopt;
  return priority;
}

inline auto
read_nice_value(HANDLE handle) -> expected<int, std::error_code>
{
  if (!handle)
    return unexpected(std::make_error_code(std::errc::no_such_process));
  int const priority = GetThreadPriority(handle);
  if (priority == THREAD_PRIORITY_ERROR_RETURN)
    return unexpected(last_win32_error());
  if (priority >= THREAD_PRIORITY_HIGHEST)
    return -20;
  if (priority == THREAD_PRIORITY_ABOVE_NORMAL)
    return -5;
  if (priority == THREAD_PRIORITY_NORMAL)
    return 0;
  if (priority == THREAD_PRIORITY_BELOW_NORMAL)
    return 5;
  if (priority == THREAD_PRIORITY_LOWEST)
    return 10;
  return 19;
}

inline auto
read_scheduling_policy(HANDLE handle) -> std::optional<native_scheduling_policy>
{
  if (!handle)
    return std::nullopt;
  return native_scheduling_policy::other;
}

inline auto
apply_priority(native_thread_id tid, native_thread_priority priority) -> expected<void, std::error_code>
{
  HANDLE handle = OpenThread(THREAD_SET_INFORMATION, FALSE, tid);
  if (!handle)
    return unexpected(last_win32_error());

  auto result = apply_priority(handle, priority);
  CloseHandle(handle);
  return result;
}

inline auto
apply_windows_thread_priority(native_thread_id tid, int priority) -> expected<void, std::error_code>
{
  HANDLE handle = OpenThread(THREAD_SET_INFORMATION, FALSE, tid);
  if (!handle)
    return unexpected(last_win32_error());
  auto result = apply_windows_thread_priority(handle, priority);
  CloseHandle(handle);
  return result;
}

inline auto
apply_nice_value(native_thread_id tid, int nice_value) -> expected<void, std::error_code>
{
  HANDLE handle = OpenThread(THREAD_SET_INFORMATION, FALSE, tid);
  if (!handle)
    return unexpected(last_win32_error());
  auto result = apply_nice_value(handle, nice_value);
  CloseHandle(handle);
  return result;
}

inline auto
apply_scheduling_policy(native_thread_id tid, native_scheduling_policy policy, native_thread_priority priority)
    -> expected<void, std::error_code>
{
  HANDLE handle = OpenThread(THREAD_SET_INFORMATION, FALSE, tid);
  if (!handle)
    return unexpected(last_win32_error());

  auto result = apply_scheduling_policy(handle, policy, priority);
  CloseHandle(handle);
  return result;
}

inline auto
apply_affinity(native_thread_id tid, native_thread_affinity const& affinity) -> expected<void, std::error_code>
{
  HANDLE handle = OpenThread(THREAD_SET_INFORMATION, FALSE, tid);
  if (!handle)
    return unexpected(last_win32_error());

  auto result = apply_affinity(handle, affinity);
  CloseHandle(handle);
  return result;
}

inline auto
apply_name(native_thread_id tid, std::string const& name) -> expected<void, std::error_code>
{
  HANDLE handle = OpenThread(THREAD_SET_LIMITED_INFORMATION, FALSE, tid);
  if (!handle)
    return unexpected(std::make_error_code(std::errc::no_such_process));

  auto result = apply_name(handle, name);
  CloseHandle(handle);
  return result;
}

inline auto
read_name(native_thread_id tid) -> expected<std::string, std::error_code>
{
  HANDLE handle = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
  if (!handle)
    return unexpected(last_win32_error());

  auto result = read_name(handle);
  CloseHandle(handle);
  return result;
}

inline auto
read_affinity(native_thread_id tid) -> expected<native_thread_affinity, std::error_code>
{
  HANDLE handle = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
  if (!handle)
    return unexpected(last_win32_error());

  auto result = read_affinity(handle);
  CloseHandle(handle);
  return result;
}

inline auto
read_priority(native_thread_id tid) -> std::optional<int>
{
  HANDLE handle = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
  if (!handle)
    return std::nullopt;

  auto result = read_priority(handle);
  CloseHandle(handle);
  return result;
}

inline auto
read_nice_value(native_thread_id tid) -> expected<int, std::error_code>
{
  HANDLE handle = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
  if (!handle)
    return unexpected(last_win32_error());
  auto result = read_nice_value(handle);
  CloseHandle(handle);
  return result;
}

inline auto
read_scheduling_policy(native_thread_id tid) -> std::optional<native_scheduling_policy>
{
  HANDLE handle = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
  if (!handle)
    return std::nullopt;

  auto result = read_scheduling_policy(handle);
  CloseHandle(handle);
  return result;
}

#if defined(__MINGW32__)
// winpthreads' pthread_t is an opaque identifier.  Keep conversion to a Win32
// handle in this adapter; it must never participate in the native_thread_id
// overload set.
inline auto
win32_handle_from_pthread(pthread_t thread) -> expected<HANDLE, std::error_code>
{
  HANDLE const handle = pthread_gethandle(thread);
  if (!handle)
    return unexpected(last_win32_error());
  return handle;
}

inline auto
apply_scheduling_policy(pthread_t thread, native_scheduling_policy policy, native_thread_priority priority)
    -> expected<void, std::error_code>
{
  auto const handle = win32_handle_from_pthread(thread);
  if (!handle.has_value())
    return unexpected(handle.error());
  return apply_scheduling_policy(handle.value(), policy, priority);
}

inline auto
apply_priority(pthread_t thread, native_thread_priority priority) -> expected<void, std::error_code>
{
  auto const handle = win32_handle_from_pthread(thread);
  if (!handle.has_value())
    return unexpected(handle.error());
  return apply_priority(handle.value(), priority);
}

inline auto
apply_windows_thread_priority(pthread_t thread, int priority) -> expected<void, std::error_code>
{
  auto const handle = win32_handle_from_pthread(thread);
  if (!handle.has_value())
    return unexpected(handle.error());
  return apply_windows_thread_priority(handle.value(), priority);
}

inline auto
apply_nice_value(pthread_t thread, int nice_value) -> expected<void, std::error_code>
{
  auto const handle = win32_handle_from_pthread(thread);
  if (!handle.has_value())
    return unexpected(handle.error());
  return apply_nice_value(handle.value(), nice_value);
}

inline auto
apply_affinity(pthread_t thread, native_thread_affinity const& affinity) -> expected<void, std::error_code>
{
  auto const handle = win32_handle_from_pthread(thread);
  if (!handle.has_value())
    return unexpected(handle.error());
  return apply_affinity(handle.value(), affinity);
}

inline auto
apply_name(pthread_t thread, std::string const& name) -> expected<void, std::error_code>
{
  // Validate before handing UTF-8 to winpthreads, whose API accepts char*.
  auto const wide = utf8_to_utf16(name);
  if (!wide.has_value())
    return unexpected(wide.error());
  int const result = pthread_setname_np(thread, name.c_str());
  if (result == 0)
    return {};
  return unexpected(std::error_code(result, std::generic_category()));
}

inline auto
read_name(pthread_t thread) -> expected<std::string, std::error_code>
{
  std::array<char, 256> name{};
  int const result = pthread_getname_np(thread, name.data(), name.size());
  if (result != 0)
    return unexpected(std::error_code(result, std::generic_category()));
  return std::string(name.data());
}

inline auto
read_affinity(pthread_t thread) -> expected<native_thread_affinity, std::error_code>
{
  auto const handle = win32_handle_from_pthread(thread);
  if (!handle.has_value())
    return unexpected(handle.error());
  return read_affinity(handle.value());
}

inline auto
read_priority(pthread_t thread) -> std::optional<int>
{
  auto const handle = win32_handle_from_pthread(thread);
  return handle.has_value() ? read_priority(handle.value()) : std::nullopt;
}

inline auto
read_nice_value(pthread_t thread) -> expected<int, std::error_code>
{
  auto const handle = win32_handle_from_pthread(thread);
  if (!handle.has_value())
    return unexpected(handle.error());
  return read_nice_value(handle.value());
}

inline auto
read_scheduling_policy(pthread_t thread) -> std::optional<native_scheduling_policy>
{
  auto const handle = win32_handle_from_pthread(thread);
  return handle.has_value() ? read_scheduling_policy(handle.value()) : std::nullopt;
}
#endif
