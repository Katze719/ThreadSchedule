#include <threadschedule/abi.hpp>
#include <threadschedule/thread_registry.hpp>
#include <string>

namespace threadschedule
{

static ThreadRegistry* g_external = nullptr;
static ThreadRegistry g_local;

static auto resolve_registry(abi::registry_handle handle) noexcept -> ThreadRegistry*
{
    return static_cast<ThreadRegistry*>(handle.opaque);
}

static auto active_registry() noexcept -> ThreadRegistry&
{
    return (g_external != nullptr) ? *g_external : g_local;
}

THREADSCHEDULE_API auto detail::runtime_registry() -> ThreadRegistry&
{
    return active_registry();
}

THREADSCHEDULE_API void detail::runtime_set_external_registry(ThreadRegistry* reg)
{
    g_external = reg;
}

THREADSCHEDULE_API auto registry() -> ThreadRegistry&
{
    return detail::runtime_registry();
}

THREADSCHEDULE_API void set_external_registry(ThreadRegistry* reg)
{
    detail::runtime_set_external_registry(reg);
}

THREADSCHEDULE_API auto build_mode() -> BuildMode
{
    return BuildMode::RUNTIME;
}

} // namespace threadschedule

extern "C"
{

THREADSCHEDULE_API auto threadschedule_abi_registry_create() noexcept -> ::threadschedule::abi::registry_handle
{
    return ::threadschedule::abi::registry_handle{new ::threadschedule::ThreadRegistry()};
}

THREADSCHEDULE_API void threadschedule_abi_registry_destroy(::threadschedule::abi::registry_handle handle) noexcept
{
    auto* reg = static_cast<::threadschedule::ThreadRegistry*>(handle.opaque);
    if (reg == nullptr || reg == &::threadschedule::g_local)
        return;
    if (::threadschedule::g_external == reg)
        ::threadschedule::g_external = nullptr;
    delete reg;
}

THREADSCHEDULE_API auto threadschedule_abi_registry_current() noexcept -> ::threadschedule::abi::registry_handle
{
    return ::threadschedule::abi::registry_handle{&::threadschedule::active_registry()};
}

THREADSCHEDULE_API void threadschedule_abi_registry_set_external(::threadschedule::abi::registry_handle handle) noexcept
{
    ::threadschedule::g_external = static_cast<::threadschedule::ThreadRegistry*>(handle.opaque);
}

THREADSCHEDULE_API auto threadschedule_abi_registry_count(::threadschedule::abi::registry_handle handle) noexcept
    -> std::size_t
{
    auto* reg = ::threadschedule::resolve_registry(handle);
    return reg != nullptr ? reg->count() : 0U;
}

THREADSCHEDULE_API auto threadschedule_abi_registry_for_each(::threadschedule::abi::registry_handle handle,
                                                             ::threadschedule::abi::thread_info_callback callback,
                                                             void* user_data) noexcept -> ::threadschedule::abi::status
{
    auto* reg = ::threadschedule::resolve_registry(handle);
    if (reg == nullptr || callback == nullptr)
        return {::threadschedule::abi::status_code::invalid_argument};

    reg->for_each([&](::threadschedule::RegisteredThreadInfo const& info) {
        ::threadschedule::abi::thread_info_view view{
            info.tid,
            {info.name.data(), info.name.size()},
            {info.componentTag.data(), info.componentTag.size()},
            static_cast<std::uint8_t>(info.alive ? 1U : 0U),
            static_cast<std::uint8_t>(info.control ? 1U : 0U),
            {0, 0, 0, 0, 0, 0},
        };
        callback(&view, user_data);
    });

    return {::threadschedule::abi::status_code::ok};
}

THREADSCHEDULE_API auto threadschedule_abi_registry_register_current_thread(::threadschedule::abi::registry_handle handle,
                                                                            char const* name, std::size_t name_size,
                                                                            char const* component_tag,
                                                                            std::size_t component_tag_size) noexcept
    -> ::threadschedule::abi::status
{
    auto* reg = ::threadschedule::resolve_registry(handle);
    if (reg == nullptr)
        return {::threadschedule::abi::status_code::invalid_argument};

    std::string const name_value = (name != nullptr) ? std::string(name, name_size) : std::string();
    std::string const component_value =
        (component_tag != nullptr) ? std::string(component_tag, component_tag_size) : std::string();

    auto block = ::threadschedule::ThreadControlBlock::create_for_current_thread();
    (void)block->set_name(name_value);
    reg->register_current_thread(block, name_value, component_value);
    return {::threadschedule::abi::status_code::ok};
}

THREADSCHEDULE_API auto threadschedule_abi_registry_unregister_current_thread(
    ::threadschedule::abi::registry_handle handle) noexcept -> ::threadschedule::abi::status
{
    auto* reg = ::threadschedule::resolve_registry(handle);
    if (reg == nullptr)
        return {::threadschedule::abi::status_code::invalid_argument};

    reg->unregister_current_thread();
    return {::threadschedule::abi::status_code::ok};
}

} // extern "C"
