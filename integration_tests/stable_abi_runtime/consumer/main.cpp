#include <threadschedule/abi.hpp>

#include <atomic>
#include <cstddef>
#include <iostream>
#include <string>

namespace ts_abi = threadschedule::abi;

extern "C"
{
auto stable_abi_producer_register(ts_abi::registry_handle registry) noexcept -> ts_abi::status;
auto stable_abi_producer_unregister(ts_abi::registry_handle registry) noexcept -> ts_abi::status;
auto stable_abi_producer_enumerate(ts_abi::registry_handle registry,
                                   ts_abi::thread_info_callback callback,
                                   void* user_data) noexcept -> ts_abi::status;
auto stable_abi_producer_pool_create(ts_abi::pool_config const* config) noexcept -> ts_abi::pool_handle;
auto stable_abi_producer_pool_post(ts_abi::pool_handle pool,
                                   ts_abi::task_callback callback,
                                   void* user_data,
                                   ts_abi::task_completion_callback completion,
                                   void* completion_user_data) noexcept -> ts_abi::status;
auto stable_abi_producer_pool_wait(ts_abi::pool_handle pool) noexcept -> ts_abi::status;
auto stable_abi_producer_pool_stats(ts_abi::pool_handle pool, ts_abi::pool_stats_view* stats) noexcept
    -> ts_abi::status;
auto stable_abi_producer_pool_shutdown(ts_abi::pool_handle pool, ts_abi::shutdown_policy policy) noexcept
    -> ts_abi::status;
void stable_abi_producer_pool_destroy(ts_abi::pool_handle pool) noexcept;
}

namespace
{
struct SeenThreads
{
    std::size_t total = 0;
    bool saw_producer_name = false;
    bool saw_component = false;
};

struct PoolState
{
    std::atomic<int> ran{0};
    std::atomic<int> completed{0};
};

auto copy(ts_abi::string_ref value) -> std::string
{
    return std::string(value.view());
}

void collect_thread(ts_abi::thread_info_view const* info, void* user_data) noexcept
{
    if (info == nullptr || user_data == nullptr)
        return;

    auto& seen = *static_cast<SeenThreads*>(user_data);
    ++seen.total;

    auto const name = copy(info->name);
    auto const component = copy(info->component_tag);
    seen.saw_producer_name = seen.saw_producer_name || name == "stable-abi-producer";
    seen.saw_component = seen.saw_component || component == "stable-abi-runtime";
}

void run_pool_task(void* user_data) noexcept
{
    auto& state = *static_cast<PoolState*>(user_data);
    state.ran.fetch_add(1, std::memory_order_relaxed);
}

void complete_pool_task(ts_abi::status status, void* user_data) noexcept
{
    auto& state = *static_cast<PoolState*>(user_data);
    if (ts_abi::succeeded(status))
        state.completed.fetch_add(1, std::memory_order_relaxed);
}

auto fail(char const* message) -> int
{
    std::cerr << message << '\n';
    return 1;
}
} // namespace

int main()
{
    if (ts_abi::runtime_abi_version() != ts_abi::abi_version)
        return fail("runtime ABI version mismatch");

    auto const registry = ts_abi::create_registry();
    if (!registry.valid())
        return fail("failed to create ABI registry");

    auto const cleanup = [&registry] {
        (void)stable_abi_producer_unregister(registry);
        ts_abi::destroy_registry(registry);
    };

    if (ts_abi::registry_count(registry) != 0U)
    {
        cleanup();
        return fail("new ABI registry was not empty");
    }

    if (!ts_abi::succeeded(stable_abi_producer_register(registry)))
    {
        cleanup();
        return fail("producer failed to register current thread");
    }

    if (ts_abi::registry_count(registry) != 1U)
    {
        cleanup();
        return fail("ABI registry count did not include producer registration");
    }

    SeenThreads seen{};
    auto const enumerate_status = stable_abi_producer_enumerate(registry, &collect_thread, &seen);
    if (!ts_abi::succeeded(enumerate_status))
    {
        cleanup();
        return fail("producer failed to enumerate ABI registry");
    }

    if (seen.total != 1U || !seen.saw_producer_name || !seen.saw_component)
    {
        cleanup();
        return fail("consumer did not observe producer registration through ABI enumeration");
    }

    if (ts_abi::succeeded(stable_abi_producer_register({})))
    {
        cleanup();
        return fail("invalid ABI registry handle unexpectedly succeeded");
    }

    if (!ts_abi::succeeded(stable_abi_producer_unregister(registry)))
    {
        cleanup();
        return fail("producer failed to unregister current thread");
    }

    if (ts_abi::registry_count(registry) != 0U)
    {
        cleanup();
        return fail("ABI registry count did not return to zero");
    }

    ts_abi::pool_config pool_config{};
    pool_config.worker_count = 2;
    auto pool = stable_abi_producer_pool_create(&pool_config);
    if (!pool.valid())
    {
        cleanup();
        return fail("producer failed to create ABI pool");
    }

    PoolState pool_state{};
    auto const post_status =
        stable_abi_producer_pool_post(pool, &run_pool_task, &pool_state, &complete_pool_task, &pool_state);
    if (!ts_abi::succeeded(post_status))
    {
        stable_abi_producer_pool_destroy(pool);
        cleanup();
        return fail("producer failed to post ABI pool task");
    }

    if (!ts_abi::succeeded(stable_abi_producer_pool_wait(pool)))
    {
        stable_abi_producer_pool_destroy(pool);
        cleanup();
        return fail("producer failed to wait for ABI pool");
    }

    ts_abi::pool_stats_view stats{};
    if (!ts_abi::succeeded(stable_abi_producer_pool_stats(pool, &stats)))
    {
        stable_abi_producer_pool_destroy(pool);
        cleanup();
        return fail("producer failed to read ABI pool stats");
    }

    if (pool_state.ran.load(std::memory_order_relaxed) != 1 ||
        pool_state.completed.load(std::memory_order_relaxed) != 1 || stats.submitted != 1U ||
        stats.completed != 1U || stats.failed != 0U || stats.worker_count != 2U)
    {
        stable_abi_producer_pool_destroy(pool);
        cleanup();
        return fail("ABI pool task execution or stats were incorrect");
    }

    if (!ts_abi::succeeded(stable_abi_producer_pool_shutdown(pool, ts_abi::shutdown_policy::drain)))
    {
        stable_abi_producer_pool_destroy(pool);
        cleanup();
        return fail("producer failed to shut down ABI pool");
    }

    if (ts_abi::succeeded(stable_abi_producer_pool_post(pool, &run_pool_task, &pool_state, nullptr, nullptr)))
    {
        stable_abi_producer_pool_destroy(pool);
        cleanup();
        return fail("ABI pool post after shutdown unexpectedly succeeded");
    }

    stable_abi_producer_pool_destroy(pool);
    ts_abi::destroy_registry(registry);
    return 0;
}
