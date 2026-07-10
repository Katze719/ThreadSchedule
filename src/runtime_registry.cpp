#include <threadschedule/abi.hpp>
#include <threadschedule/thread_pool.hpp>
#include <threadschedule/thread_registry.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

namespace threadschedule
{

static ThreadRegistry* g_external = nullptr;
static ThreadRegistry g_local;

namespace
{
constexpr std::uint64_t registry_handle_magic = 0x5453524547495354ULL; // TSREGIST
constexpr std::uint64_t pool_handle_magic = 0x5453504f4f4c3030ULL;     // TSPOOL00

struct RegistryHandleState
{
    std::uint64_t magic = registry_handle_magic;
    bool owns_registry = false;
    ThreadRegistry* registry = nullptr;
};

struct PoolHandleState
{
    std::uint64_t magic = pool_handle_magic;
    std::unique_ptr<ThreadPool> pool{};
    std::atomic<std::uint64_t> submitted{0};
    std::atomic<std::uint64_t> completed{0};
    std::atomic<std::uint64_t> failed{0};
};

static RegistryHandleState g_current_registry_handle{registry_handle_magic, false, &g_local};
} // namespace

static auto resolve_registry(abi::registry_handle handle) noexcept -> ThreadRegistry*
{
    auto* state = static_cast<RegistryHandleState*>(handle.opaque);
    if (state == nullptr || state->magic != registry_handle_magic)
        return nullptr;
    return state->registry;
}

static auto active_registry() noexcept -> ThreadRegistry&
{
    return (g_external != nullptr) ? *g_external : g_local;
}

static auto resolve_pool(abi::pool_handle handle) noexcept -> PoolHandleState*
{
    auto* state = static_cast<PoolHandleState*>(handle.opaque);
    if (state == nullptr || state->magic != pool_handle_magic || state->pool == nullptr)
        return nullptr;
    return state;
}

static auto to_abi_status(std::error_code const& error) noexcept -> abi::status
{
    if (!error)
        return {abi::status_code::ok};
    if (error == std::make_error_code(std::errc::invalid_argument) ||
        error == std::make_error_code(std::errc::no_such_process))
        return {abi::status_code::invalid_argument};
    if (error == std::make_error_code(std::errc::operation_canceled))
        return {abi::status_code::shutdown};
    if (error == std::make_error_code(std::errc::operation_not_permitted) ||
        error == std::make_error_code(std::errc::permission_denied))
        return {abi::status_code::permission_denied};
    if (error == std::make_error_code(std::errc::function_not_supported))
        return {abi::status_code::not_supported};
    return {abi::status_code::runtime_error};
}

static auto to_scheduling_policy(abi::scheduling_policy policy) noexcept -> SchedulingPolicy
{
    switch (policy)
    {
    case abi::scheduling_policy::fifo:
        return SchedulingPolicy::FIFO;
    case abi::scheduling_policy::round_robin:
        return SchedulingPolicy::RR;
    case abi::scheduling_policy::batch:
        return SchedulingPolicy::BATCH;
    case abi::scheduling_policy::idle:
        return SchedulingPolicy::IDLE;
    case abi::scheduling_policy::normal:
    default:
        return SchedulingPolicy::OTHER;
    }
}

static auto to_thread_scheduling_config(abi::scheduling_request const& request) noexcept -> ThreadSchedulingConfig
{
    auto const policy = to_scheduling_policy(request.policy);
    switch (request.priority_type)
    {
    case abi::priority_kind::posix_nice:
        return schedule::posix_nice(request.priority_value);
    case abi::priority_kind::posix_realtime:
        return policy == SchedulingPolicy::FIFO ? schedule::realtime_fifo(request.priority_value)
                                                 : schedule::realtime_rr(request.priority_value);
    case abi::priority_kind::windows_thread:
        return schedule::native_windows_priority(request.priority_value);
    case abi::priority_kind::platform_native:
        return schedule::native(policy, ThreadPriority{request.priority_value});
    case abi::priority_kind::intent:
    default:
        break;
    }

    switch (request.intent)
    {
    case abi::scheduling_intent::background:
        return schedule::background();
    case abi::scheduling_intent::interactive:
        return schedule::interactive();
    case abi::scheduling_intent::low_latency:
        return schedule::low_latency();
    case abi::scheduling_intent::realtime:
        return policy == SchedulingPolicy::FIFO ? schedule::realtime_fifo(request.priority_value > 0 ? request.priority_value : 80)
                                                : schedule::realtime_rr(request.priority_value > 0 ? request.priority_value : 80);
    case abi::scheduling_intent::normal:
    default:
        return schedule::normal();
    }
}

static auto has_non_default_scheduling(abi::scheduling_request const& request) noexcept -> bool
{
    return request.policy != abi::scheduling_policy::normal || request.intent != abi::scheduling_intent::normal ||
           request.priority_type != abi::priority_kind::intent || request.priority_value != 0 || request.flags != 0;
}

static auto to_pool_shutdown_policy(abi::shutdown_policy policy) noexcept -> ShutdownPolicy
{
    return policy == abi::shutdown_policy::drop_pending ? ShutdownPolicy::drop_pending : ShutdownPolicy::drain;
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

THREADSCHEDULE_API auto threadschedule_abi_version() noexcept -> std::uint32_t
{
    return ::threadschedule::abi::abi_version;
}

THREADSCHEDULE_API auto threadschedule_abi_registry_create() noexcept -> ::threadschedule::abi::registry_handle
{
    try
    {
        auto* registry = new ::threadschedule::ThreadRegistry();
        auto* state = new ::threadschedule::RegistryHandleState{
            ::threadschedule::registry_handle_magic,
            true,
            registry,
        };
        return ::threadschedule::abi::registry_handle{state};
    }
    catch (...)
    {
        return {};
    }
}

THREADSCHEDULE_API void threadschedule_abi_registry_destroy(::threadschedule::abi::registry_handle handle) noexcept
{
    auto* state = static_cast<::threadschedule::RegistryHandleState*>(handle.opaque);
    if (state == nullptr || state == &::threadschedule::g_current_registry_handle ||
        state->magic != ::threadschedule::registry_handle_magic)
        return;
    if (::threadschedule::g_external == state->registry)
        ::threadschedule::g_external = nullptr;
    auto* registry = state->registry;
    state->magic = 0;
    state->registry = nullptr;
    if (state->owns_registry)
        delete registry;
    delete state;
}

THREADSCHEDULE_API auto threadschedule_abi_registry_current() noexcept -> ::threadschedule::abi::registry_handle
{
    ::threadschedule::g_current_registry_handle.registry = &::threadschedule::active_registry();
    return ::threadschedule::abi::registry_handle{&::threadschedule::g_current_registry_handle};
}

THREADSCHEDULE_API void threadschedule_abi_registry_set_external(::threadschedule::abi::registry_handle handle) noexcept
{
    if (!handle.valid())
    {
        ::threadschedule::g_external = nullptr;
        return;
    }
    ::threadschedule::g_external = ::threadschedule::resolve_registry(handle);
}

THREADSCHEDULE_API auto threadschedule_abi_registry_count(::threadschedule::abi::registry_handle handle) noexcept
    -> ::threadschedule::abi::abi_size
{
    try
    {
        auto* reg = ::threadschedule::resolve_registry(handle);
        return reg != nullptr ? static_cast<::threadschedule::abi::abi_size>(reg->count()) : 0U;
    }
    catch (...)
    {
        return 0U;
    }
}

THREADSCHEDULE_API auto threadschedule_abi_registry_for_each(::threadschedule::abi::registry_handle handle,
                                                             ::threadschedule::abi::thread_info_callback callback,
                                                             void* user_data) noexcept -> ::threadschedule::abi::status
{
    try
    {
        auto* reg = ::threadschedule::resolve_registry(handle);
        if (reg == nullptr || callback == nullptr)
            return {::threadschedule::abi::status_code::invalid_argument};

        reg->for_each([&](::threadschedule::RegisteredThreadInfo const& info) {
            ::threadschedule::abi::thread_info_view view{};
            view.tid = static_cast<::threadschedule::abi::abi_tid>(info.tid);
            view.name = {info.name.data(), static_cast<::threadschedule::abi::abi_size>(info.name.size())};
            view.component_tag = {info.componentTag.data(),
                                  static_cast<::threadschedule::abi::abi_size>(info.componentTag.size())};
            view.alive = static_cast<std::uint8_t>(info.alive ? 1U : 0U);
            view.has_control_block = static_cast<std::uint8_t>(info.control ? 1U : 0U);
            callback(&view, user_data);
        });

        return {::threadschedule::abi::status_code::ok};
    }
    catch (...)
    {
        return {::threadschedule::abi::status_code::runtime_error};
    }
}

THREADSCHEDULE_API auto threadschedule_abi_registry_register_current_thread(::threadschedule::abi::registry_handle handle,
                                                                            char const* name,
                                                                            ::threadschedule::abi::abi_size name_size,
                                                                            char const* component_tag,
                                                                            ::threadschedule::abi::abi_size
                                                                                component_tag_size) noexcept
    -> ::threadschedule::abi::status
{
    try
    {
        auto* reg = ::threadschedule::resolve_registry(handle);
        if (reg == nullptr)
            return {::threadschedule::abi::status_code::invalid_argument};

        std::string const name_value =
            (name != nullptr) ? std::string(name, static_cast<std::size_t>(name_size)) : std::string();
        std::string const component_value =
            (component_tag != nullptr) ? std::string(component_tag, static_cast<std::size_t>(component_tag_size))
                                       : std::string();

        auto block = ::threadschedule::ThreadControlBlock::create_for_current_thread();
        (void)block->set_name(name_value);
        reg->register_current_thread(block, name_value, component_value);
        return {::threadschedule::abi::status_code::ok};
    }
    catch (...)
    {
        return {::threadschedule::abi::status_code::runtime_error};
    }
}

THREADSCHEDULE_API auto threadschedule_abi_registry_unregister_current_thread(
    ::threadschedule::abi::registry_handle handle) noexcept -> ::threadschedule::abi::status
{
    try
    {
        auto* reg = ::threadschedule::resolve_registry(handle);
        if (reg == nullptr)
            return {::threadschedule::abi::status_code::invalid_argument};

        reg->unregister_current_thread();
        return {::threadschedule::abi::status_code::ok};
    }
    catch (...)
    {
        return {::threadschedule::abi::status_code::runtime_error};
    }
}

THREADSCHEDULE_API auto threadschedule_abi_pool_create(::threadschedule::abi::pool_config const* config) noexcept
    -> ::threadschedule::abi::pool_handle
{
    try
    {
        std::size_t worker_count = std::thread::hardware_concurrency();
        if (config != nullptr && config->worker_count != 0)
            worker_count = config->worker_count;
        if (worker_count == 0)
            worker_count = 1;

        auto* state = new ::threadschedule::PoolHandleState();
        state->pool = std::make_unique<::threadschedule::ThreadPool>(worker_count, true);

        if (config != nullptr && ::threadschedule::has_non_default_scheduling(config->worker_scheduling))
        {
            ::threadschedule::ThreadConfig worker_config{};
            worker_config.name = "ts_pool";
            worker_config.scheduling = ::threadschedule::to_thread_scheduling_config(config->worker_scheduling);
            (void)state->pool->configure_threads(worker_config);
        }

        return ::threadschedule::abi::pool_handle{state};
    }
    catch (...)
    {
        return {};
    }
}

THREADSCHEDULE_API void threadschedule_abi_pool_destroy(::threadschedule::abi::pool_handle handle) noexcept
{
    auto* state = static_cast<::threadschedule::PoolHandleState*>(handle.opaque);
    if (state == nullptr || state->magic != ::threadschedule::pool_handle_magic)
        return;
    state->magic = 0;
    try
    {
        if (state->pool)
            state->pool->shutdown(::threadschedule::ShutdownPolicy::drain);
    }
    catch (...)
    {
    }
    delete state;
}

THREADSCHEDULE_API auto threadschedule_abi_pool_post(::threadschedule::abi::pool_handle handle,
                                                     ::threadschedule::abi::task_callback callback,
                                                     void* user_data,
                                                     ::threadschedule::abi::task_completion_callback completion,
                                                     void* completion_user_data) noexcept
    -> ::threadschedule::abi::status
{
    try
    {
        auto* state = ::threadschedule::resolve_pool(handle);
        if (state == nullptr || callback == nullptr)
            return {::threadschedule::abi::status_code::invalid_argument};

        auto result = state->pool->try_post([state, callback, user_data, completion, completion_user_data]() {
            ::threadschedule::abi::status task_status{::threadschedule::abi::status_code::ok};
            try
            {
                callback(user_data);
                state->completed.fetch_add(1, std::memory_order_relaxed);
            }
            catch (...)
            {
                task_status = {::threadschedule::abi::status_code::runtime_error};
                state->failed.fetch_add(1, std::memory_order_relaxed);
            }
            if (completion != nullptr)
                completion(task_status, completion_user_data);
        });

        if (!result.has_value())
            return ::threadschedule::to_abi_status(result.error());
        state->submitted.fetch_add(1, std::memory_order_relaxed);
        return {::threadschedule::abi::status_code::ok};
    }
    catch (...)
    {
        return {::threadschedule::abi::status_code::runtime_error};
    }
}

THREADSCHEDULE_API auto threadschedule_abi_pool_wait(::threadschedule::abi::pool_handle handle) noexcept
    -> ::threadschedule::abi::status
{
    try
    {
        auto* state = ::threadschedule::resolve_pool(handle);
        if (state == nullptr)
            return {::threadschedule::abi::status_code::invalid_argument};
        state->pool->wait_for_tasks();
        return {::threadschedule::abi::status_code::ok};
    }
    catch (...)
    {
        return {::threadschedule::abi::status_code::runtime_error};
    }
}

THREADSCHEDULE_API auto threadschedule_abi_pool_shutdown(::threadschedule::abi::pool_handle handle,
                                                         ::threadschedule::abi::shutdown_policy policy) noexcept
    -> ::threadschedule::abi::status
{
    try
    {
        auto* state = ::threadschedule::resolve_pool(handle);
        if (state == nullptr)
            return {::threadschedule::abi::status_code::invalid_argument};
        state->pool->shutdown(::threadschedule::to_pool_shutdown_policy(policy));
        return {::threadschedule::abi::status_code::ok};
    }
    catch (...)
    {
        return {::threadschedule::abi::status_code::runtime_error};
    }
}

THREADSCHEDULE_API auto threadschedule_abi_pool_stats(::threadschedule::abi::pool_handle handle,
                                                      ::threadschedule::abi::pool_stats_view* out_stats) noexcept
    -> ::threadschedule::abi::status
{
    try
    {
        auto* state = ::threadschedule::resolve_pool(handle);
        if (state == nullptr || out_stats == nullptr)
            return {::threadschedule::abi::status_code::invalid_argument};

        auto const stats = state->pool->get_statistics();
        ::threadschedule::abi::pool_stats_view view{};
        view.submitted = state->submitted.load(std::memory_order_relaxed);
        view.completed = state->completed.load(std::memory_order_relaxed);
        view.failed = state->failed.load(std::memory_order_relaxed);
        view.pending = static_cast<::threadschedule::abi::abi_size>(stats.pending_tasks);
        view.worker_count = static_cast<std::uint32_t>(stats.total_threads);
        *out_stats = view;
        return {::threadschedule::abi::status_code::ok};
    }
    catch (...)
    {
        return {::threadschedule::abi::status_code::runtime_error};
    }
}

} // extern "C"
