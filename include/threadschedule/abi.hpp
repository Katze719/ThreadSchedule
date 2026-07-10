#pragma once

/**
 * @file abi.hpp
 * @brief Stable ABI helpers and runtime-safe registry accessors.
 */

#include "export.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace threadschedule::abi
{

inline constexpr std::uint32_t abi_version_major = 3;
inline constexpr std::uint32_t abi_version_minor = 0;
inline constexpr std::uint32_t abi_version_patch = 0;
inline constexpr std::uint32_t abi_version =
    (abi_version_major << 24U) | (abi_version_minor << 12U) | abi_version_patch;

using abi_size = std::uint64_t;
using abi_tid = std::uint64_t;
using abi_duration_ns = std::int64_t;

struct string_ref
{
    char const* data = nullptr;
    abi_size size = 0;

    [[nodiscard]] constexpr auto view() const noexcept -> std::string_view
    {
        return std::string_view(data != nullptr ? data : "", static_cast<std::size_t>(size));
    }
};

struct registry_handle
{
    void* opaque = nullptr;

    [[nodiscard]] constexpr auto valid() const noexcept -> bool
    {
        return opaque != nullptr;
    }
};

struct thread_handle
{
    void* opaque = nullptr;

    [[nodiscard]] constexpr auto valid() const noexcept -> bool
    {
        return opaque != nullptr;
    }
};

struct pool_handle
{
    void* opaque = nullptr;

    [[nodiscard]] constexpr auto valid() const noexcept -> bool
    {
        return opaque != nullptr;
    }
};

struct scheduled_pool_handle
{
    void* opaque = nullptr;

    [[nodiscard]] constexpr auto valid() const noexcept -> bool
    {
        return opaque != nullptr;
    }
};

struct scheduled_task_handle
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr auto valid() const noexcept -> bool
    {
        return value != 0;
    }
};

enum class status_code : std::uint32_t
{
    ok = 0,
    invalid_argument = 1,
    runtime_error = 2,
    not_supported = 3,
    permission_denied = 4,
    shutdown = 5,
};

struct status
{
    constexpr status(status_code status_code_value = status_code::ok) noexcept
        : size(sizeof(status)), version(abi_version), code(status_code_value)
    {
    }

    std::uint32_t size = 0;
    std::uint32_t version = 0;
    status_code code = status_code::ok;
    std::uint32_t reserved = 0;
};

enum class scheduling_policy : std::uint32_t
{
    normal = 0,
    fifo = 1,
    round_robin = 2,
    batch = 3,
    idle = 4,
};

enum class scheduling_intent : std::uint32_t
{
    background = 0,
    normal = 1,
    interactive = 2,
    low_latency = 3,
    realtime = 4,
};

enum class priority_kind : std::uint32_t
{
    intent = 0,
    posix_nice = 1,
    posix_realtime = 2,
    windows_thread = 3,
    platform_native = 4,
};

enum class shutdown_policy : std::uint32_t
{
    drain = 0,
    drop_pending = 1,
};

struct scheduling_request
{
    constexpr scheduling_request() noexcept : size(sizeof(scheduling_request)), version(abi_version)
    {
    }

    std::uint32_t size = 0;
    std::uint32_t version = 0;
    scheduling_policy policy = scheduling_policy::normal;
    scheduling_intent intent = scheduling_intent::normal;
    priority_kind priority_type = priority_kind::intent;
    std::int32_t priority_value = 0;
    std::uint32_t flags = 0;
};

struct affinity_view
{
    std::uint64_t const* masks = nullptr;
    abi_size mask_count = 0;
};

struct pool_config
{
    constexpr pool_config() noexcept : size(sizeof(pool_config)), version(abi_version)
    {
    }

    std::uint32_t size = 0;
    std::uint32_t version = 0;
    std::uint32_t worker_count = 0;
    std::uint32_t queue_capacity = 0;
    scheduling_request worker_scheduling{};
    std::uint32_t flags = 0;
    std::uint32_t reserved = 0;
};

struct pool_stats_view
{
    constexpr pool_stats_view() noexcept : size(sizeof(pool_stats_view)), version(abi_version)
    {
    }

    std::uint32_t size = 0;
    std::uint32_t version = 0;
    std::uint64_t submitted = 0;
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
    std::uint64_t pending = 0;
    std::uint32_t worker_count = 0;
    std::uint32_t reserved = 0;
};

struct duration
{
    abi_duration_ns nanoseconds = 0;
};

using task_callback = void (*)(void*) noexcept;
using task_completion_callback = void (*)(status, void*) noexcept;
using error_callback = void (*)(status, string_ref, void*) noexcept;

struct thread_info_view
{
    constexpr thread_info_view() noexcept : size(sizeof(thread_info_view)), version(abi_version)
    {
    }

    std::uint32_t size = 0;
    std::uint32_t version = 0;
    abi_tid tid{};
    string_ref name{};
    string_ref component_tag{};
    std::uint8_t alive = 0;
    std::uint8_t has_control_block = 0;
    std::uint8_t reserved[6]{};
};

using thread_info_callback = void (*)(thread_info_view const*, void*) noexcept;

template <typename T>
struct is_abi_stable : std::false_type
{
};

template <>
struct is_abi_stable<void> : std::true_type
{
};

template <typename T>
struct is_abi_stable<T*>
    : std::bool_constant<std::is_void_v<T> || std::is_same_v<std::remove_cv_t<T>, char> ||
                         is_abi_stable<std::remove_cv_t<T>>::value>
{
};

template <typename T>
struct is_abi_stable<T const*> : is_abi_stable<T*>
{
};

template <>
struct is_abi_stable<bool> : std::true_type
{
};

template <>
struct is_abi_stable<char> : std::true_type
{
};

template <>
struct is_abi_stable<unsigned char> : std::true_type
{
};

template <>
struct is_abi_stable<std::int32_t> : std::true_type
{
};

template <>
struct is_abi_stable<std::int64_t> : std::true_type
{
};

template <>
struct is_abi_stable<std::uint32_t> : std::true_type
{
};

template <>
struct is_abi_stable<std::uint64_t> : std::true_type
{
};

template <>
struct is_abi_stable<registry_handle> : std::true_type
{
};

template <>
struct is_abi_stable<thread_handle> : std::true_type
{
};

template <>
struct is_abi_stable<pool_handle> : std::true_type
{
};

template <>
struct is_abi_stable<scheduled_pool_handle> : std::true_type
{
};

template <>
struct is_abi_stable<scheduled_task_handle> : std::true_type
{
};

template <>
struct is_abi_stable<status_code> : std::true_type
{
};

template <>
struct is_abi_stable<status> : std::true_type
{
};

template <>
struct is_abi_stable<string_ref> : std::true_type
{
};

template <>
struct is_abi_stable<scheduling_policy> : std::true_type
{
};

template <>
struct is_abi_stable<scheduling_intent> : std::true_type
{
};

template <>
struct is_abi_stable<priority_kind> : std::true_type
{
};

template <>
struct is_abi_stable<shutdown_policy> : std::true_type
{
};

template <>
struct is_abi_stable<scheduling_request> : std::true_type
{
};

template <>
struct is_abi_stable<affinity_view> : std::true_type
{
};

template <>
struct is_abi_stable<pool_config> : std::true_type
{
};

template <>
struct is_abi_stable<pool_stats_view> : std::true_type
{
};

template <>
struct is_abi_stable<duration> : std::true_type
{
};

template <>
struct is_abi_stable<thread_info_view> : std::true_type
{
};

template <>
struct is_abi_stable<thread_info_callback> : std::true_type
{
};

template <>
struct is_abi_stable<task_callback> : std::true_type
{
};

template <>
struct is_abi_stable<task_completion_callback> : std::true_type
{
};

template <>
struct is_abi_stable<error_callback> : std::true_type
{
};

template <typename T>
inline constexpr bool is_abi_stable_v = is_abi_stable<std::remove_cv_t<T>>::value;

template <typename Signature>
struct is_stable_signature : std::false_type
{
};

template <typename ReturnType, typename... Args>
struct is_stable_signature<ReturnType(Args...)>
    : std::bool_constant<is_abi_stable_v<ReturnType> && (... && is_abi_stable_v<Args>)>
{
};

template <typename Signature>
inline constexpr bool is_stable_signature_v = is_stable_signature<Signature>::value;

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
template <typename T>
concept StableAbiType = is_abi_stable_v<T>;
#endif

[[nodiscard]] constexpr auto succeeded(status value) noexcept -> bool
{
    return value.code == status_code::ok;
}

[[nodiscard]] constexpr auto make_string_ref(std::string_view value) noexcept -> string_ref
{
    return string_ref{value.data(), value.size()};
}

} // namespace threadschedule::abi

#define THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(ReturnType, ...)                                               \
    static_assert(::threadschedule::abi::is_stable_signature_v<ReturnType(__VA_ARGS__)>,                         \
                  "Signature is not part of the ThreadSchedule stable ABI subset")

#if defined(THREADSCHEDULE_RUNTIME)
extern "C"
{
THREADSCHEDULE_API auto threadschedule_abi_version() noexcept -> std::uint32_t;
THREADSCHEDULE_API auto threadschedule_abi_registry_create() noexcept -> ::threadschedule::abi::registry_handle;
THREADSCHEDULE_API void threadschedule_abi_registry_destroy(::threadschedule::abi::registry_handle handle) noexcept;
THREADSCHEDULE_API auto threadschedule_abi_registry_current() noexcept -> ::threadschedule::abi::registry_handle;
THREADSCHEDULE_API void threadschedule_abi_registry_set_external(::threadschedule::abi::registry_handle handle) noexcept;
THREADSCHEDULE_API auto threadschedule_abi_registry_count(::threadschedule::abi::registry_handle handle) noexcept
    -> ::threadschedule::abi::abi_size;
THREADSCHEDULE_API auto threadschedule_abi_registry_for_each(::threadschedule::abi::registry_handle handle,
                                                             ::threadschedule::abi::thread_info_callback callback,
                                                             void* user_data) noexcept -> ::threadschedule::abi::status;
THREADSCHEDULE_API auto threadschedule_abi_registry_register_current_thread(::threadschedule::abi::registry_handle handle,
                                                                            char const* name,
                                                                            ::threadschedule::abi::abi_size name_size,
                                                                            char const* component_tag,
                                                                            ::threadschedule::abi::abi_size
                                                                                component_tag_size) noexcept
    -> ::threadschedule::abi::status;
THREADSCHEDULE_API auto threadschedule_abi_registry_unregister_current_thread(
    ::threadschedule::abi::registry_handle handle) noexcept -> ::threadschedule::abi::status;
THREADSCHEDULE_API auto threadschedule_abi_pool_create(::threadschedule::abi::pool_config const* config) noexcept
    -> ::threadschedule::abi::pool_handle;
THREADSCHEDULE_API void threadschedule_abi_pool_destroy(::threadschedule::abi::pool_handle handle) noexcept;
THREADSCHEDULE_API auto threadschedule_abi_pool_post(::threadschedule::abi::pool_handle handle,
                                                     ::threadschedule::abi::task_callback callback,
                                                     void* user_data,
                                                     ::threadschedule::abi::task_completion_callback completion,
                                                     void* completion_user_data) noexcept
    -> ::threadschedule::abi::status;
THREADSCHEDULE_API auto threadschedule_abi_pool_wait(::threadschedule::abi::pool_handle handle) noexcept
    -> ::threadschedule::abi::status;
THREADSCHEDULE_API auto threadschedule_abi_pool_shutdown(::threadschedule::abi::pool_handle handle,
                                                         ::threadschedule::abi::shutdown_policy policy) noexcept
    -> ::threadschedule::abi::status;
THREADSCHEDULE_API auto threadschedule_abi_pool_stats(::threadschedule::abi::pool_handle handle,
                                                      ::threadschedule::abi::pool_stats_view* out_stats) noexcept
    -> ::threadschedule::abi::status;
}

namespace threadschedule::abi
{

[[nodiscard]] inline auto runtime_abi_version() noexcept -> std::uint32_t
{
    return threadschedule_abi_version();
}

[[nodiscard]] inline auto create_registry() noexcept -> registry_handle
{
    return threadschedule_abi_registry_create();
}

inline void destroy_registry(registry_handle handle) noexcept
{
    threadschedule_abi_registry_destroy(handle);
}

[[nodiscard]] inline auto current_registry() noexcept -> registry_handle
{
    return threadschedule_abi_registry_current();
}

inline void set_external_registry(registry_handle handle) noexcept
{
    threadschedule_abi_registry_set_external(handle);
}

[[nodiscard]] inline auto registry_count(registry_handle handle) noexcept -> std::size_t
{
    return static_cast<std::size_t>(threadschedule_abi_registry_count(handle));
}

inline auto registry_for_each(registry_handle handle, thread_info_callback callback, void* user_data = nullptr) noexcept
    -> status
{
    return threadschedule_abi_registry_for_each(handle, callback, user_data);
}

inline auto register_current_thread(registry_handle handle, std::string_view name = {},
                                    std::string_view component_tag = {}) noexcept -> status
{
    return threadschedule_abi_registry_register_current_thread(handle, name.data(), name.size(), component_tag.data(),
                                                               component_tag.size());
}

inline auto unregister_current_thread(registry_handle handle) noexcept -> status
{
    return threadschedule_abi_registry_unregister_current_thread(handle);
}

[[nodiscard]] inline auto create_pool(pool_config const& config = pool_config{}) noexcept -> pool_handle
{
    return threadschedule_abi_pool_create(&config);
}

inline void destroy_pool(pool_handle handle) noexcept
{
    threadschedule_abi_pool_destroy(handle);
}

inline auto pool_post(pool_handle handle, task_callback callback, void* user_data = nullptr,
                      task_completion_callback completion = nullptr, void* completion_user_data = nullptr) noexcept
    -> status
{
    return threadschedule_abi_pool_post(handle, callback, user_data, completion, completion_user_data);
}

inline auto pool_wait(pool_handle handle) noexcept -> status
{
    return threadschedule_abi_pool_wait(handle);
}

inline auto pool_shutdown(pool_handle handle, shutdown_policy policy = shutdown_policy::drain) noexcept -> status
{
    return threadschedule_abi_pool_shutdown(handle, policy);
}

inline auto pool_stats(pool_handle handle, pool_stats_view& out_stats) noexcept -> status
{
    return threadschedule_abi_pool_stats(handle, &out_stats);
}

class AutoRegisterCurrentThread
{
  public:
    explicit AutoRegisterCurrentThread(std::string_view name = {}, std::string_view component_tag = {}) noexcept
        : handle_(current_registry()),
          active_(succeeded(register_current_thread(handle_, name, component_tag)))
    {
    }

    explicit AutoRegisterCurrentThread(registry_handle handle, std::string_view name = {},
                                       std::string_view component_tag = {}) noexcept
        : handle_(handle), active_(succeeded(register_current_thread(handle_, name, component_tag)))
    {
    }

    ~AutoRegisterCurrentThread()
    {
        if (active_)
            (void)unregister_current_thread(handle_);
    }

    AutoRegisterCurrentThread(AutoRegisterCurrentThread const&) = delete;
    auto operator=(AutoRegisterCurrentThread const&) -> AutoRegisterCurrentThread& = delete;

    AutoRegisterCurrentThread(AutoRegisterCurrentThread&& other) noexcept
        : handle_(other.handle_), active_(std::exchange(other.active_, false))
    {
    }

    auto operator=(AutoRegisterCurrentThread&& other) noexcept -> AutoRegisterCurrentThread&
    {
        if (this != &other)
        {
            if (active_)
                (void)unregister_current_thread(handle_);
            handle_ = other.handle_;
            active_ = std::exchange(other.active_, false);
        }
        return *this;
    }

    [[nodiscard]] auto active() const noexcept -> bool
    {
        return active_;
    }

  private:
    registry_handle handle_{};
    bool active_ = false;
};

} // namespace threadschedule::abi
#endif
