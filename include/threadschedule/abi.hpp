#pragma once

/**
 * @file abi.hpp
 * @brief Stable ABI helpers and runtime-safe registry accessors.
 */

#include "export.hpp"
#include "scheduler_policy.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace threadschedule::abi
{

struct string_ref
{
    char const* data = nullptr;
    std::size_t size = 0;

    [[nodiscard]] constexpr auto view() const noexcept -> std::string_view
    {
        return std::string_view(data != nullptr ? data : "", size);
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

enum class status_code : std::uint32_t
{
    ok = 0,
    invalid_argument = 1,
    runtime_error = 2,
};

struct status
{
    status_code code = status_code::ok;
};

struct thread_info_view
{
    Tid tid{};
    string_ref name{};
    string_ref component_tag{};
    std::uint8_t alive = 0;
    std::uint8_t has_control_block = 0;
    std::uint8_t reserved[6]{};
};

using thread_info_callback = void (*)(thread_info_view const*, void*);

template <typename T>
struct is_abi_stable : std::false_type
{
};

template <>
struct is_abi_stable<void> : std::true_type
{
};

template <typename T>
struct is_abi_stable<T*> : std::bool_constant<std::is_void_v<T> || std::is_same_v<std::remove_cv_t<T>, char>>
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
struct is_abi_stable<std::uint32_t> : std::true_type
{
};

template <>
struct is_abi_stable<std::size_t> : std::true_type
{
};

template <>
struct is_abi_stable<Tid> : std::true_type
{
};

template <>
struct is_abi_stable<registry_handle> : std::true_type
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
struct is_abi_stable<thread_info_view> : std::true_type
{
};

template <>
struct is_abi_stable<thread_info_callback> : std::true_type
{
};

template <typename T>
inline constexpr bool is_abi_stable_v = is_abi_stable<std::remove_cv_t<std::remove_reference_t<T>>>::value;

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
THREADSCHEDULE_API auto threadschedule_abi_registry_create() noexcept -> ::threadschedule::abi::registry_handle;
THREADSCHEDULE_API void threadschedule_abi_registry_destroy(::threadschedule::abi::registry_handle handle) noexcept;
THREADSCHEDULE_API auto threadschedule_abi_registry_current() noexcept -> ::threadschedule::abi::registry_handle;
THREADSCHEDULE_API void threadschedule_abi_registry_set_external(::threadschedule::abi::registry_handle handle) noexcept;
THREADSCHEDULE_API auto threadschedule_abi_registry_count(::threadschedule::abi::registry_handle handle) noexcept
    -> std::size_t;
THREADSCHEDULE_API auto threadschedule_abi_registry_for_each(::threadschedule::abi::registry_handle handle,
                                                             ::threadschedule::abi::thread_info_callback callback,
                                                             void* user_data) noexcept -> ::threadschedule::abi::status;
THREADSCHEDULE_API auto threadschedule_abi_registry_register_current_thread(::threadschedule::abi::registry_handle handle,
                                                                            char const* name, std::size_t name_size,
                                                                            char const* component_tag,
                                                                            std::size_t component_tag_size) noexcept
    -> ::threadschedule::abi::status;
THREADSCHEDULE_API auto threadschedule_abi_registry_unregister_current_thread(
    ::threadschedule::abi::registry_handle handle) noexcept -> ::threadschedule::abi::status;
}

namespace threadschedule::abi
{

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
    return threadschedule_abi_registry_count(handle);
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
