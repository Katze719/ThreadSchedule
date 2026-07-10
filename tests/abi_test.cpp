#include <gtest/gtest.h>

#include <threadschedule/abi.hpp>
#include <threadschedule/thread_registry.hpp>
#include <atomic>
#include <type_traits>

namespace ts = threadschedule;

static_assert(ts::abi::is_abi_stable_v<ts::abi::registry_handle>);
static_assert(ts::abi::is_abi_stable_v<ts::abi::thread_handle>);
static_assert(ts::abi::is_abi_stable_v<ts::abi::pool_handle>);
static_assert(ts::abi::is_abi_stable_v<ts::abi::scheduled_pool_handle>);
static_assert(ts::abi::is_abi_stable_v<ts::abi::scheduled_task_handle>);
static_assert(ts::abi::is_abi_stable_v<ts::abi::scheduling_request>);
static_assert(ts::abi::is_abi_stable_v<ts::abi::affinity_view>);
static_assert(ts::abi::is_abi_stable_v<ts::abi::pool_config>);
static_assert(ts::abi::is_abi_stable_v<ts::abi::pool_stats_view>);
static_assert(ts::abi::is_abi_stable_v<ts::abi::thread_info_view>);
static_assert(!ts::abi::is_abi_stable_v<ts::ThreadRegistry>);
static_assert(!ts::abi::is_abi_stable_v<ts::abi::registry_handle&>);
static_assert(!ts::abi::is_abi_stable_v<ts::abi::registry_handle const&>);
static_assert(!ts::abi::is_stable_signature_v<void(ts::abi::registry_handle&)>);
static_assert(!ts::abi::is_stable_signature_v<void(ts::abi::pool_config const&)>);
static_assert(ts::abi::is_stable_signature_v<void(ts::abi::pool_config const*)>);

THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(void, ::threadschedule::abi::registry_handle);
THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(::threadschedule::abi::status,
                                          ::threadschedule::abi::pool_handle,
                                          ::threadschedule::abi::task_callback,
                                          void*);
THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(::threadschedule::abi::pool_handle,
                                          ::threadschedule::abi::pool_config const*);
THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(::threadschedule::abi::status,
                                          ::threadschedule::abi::pool_handle,
                                          ::threadschedule::abi::task_callback,
                                          void*,
                                          ::threadschedule::abi::task_completion_callback,
                                          void*);

static_assert(std::is_standard_layout_v<ts::abi::registry_handle>);
static_assert(std::is_trivially_copyable_v<ts::abi::registry_handle>);
static_assert(std::is_standard_layout_v<ts::abi::string_ref>);
static_assert(std::is_trivially_copyable_v<ts::abi::string_ref>);
static_assert(std::is_standard_layout_v<ts::abi::status>);
static_assert(std::is_trivially_copyable_v<ts::abi::status>);
static_assert(std::is_standard_layout_v<ts::abi::thread_info_view>);
static_assert(std::is_trivially_copyable_v<ts::abi::thread_info_view>);
static_assert(std::is_standard_layout_v<ts::abi::scheduling_request>);
static_assert(std::is_trivially_copyable_v<ts::abi::scheduling_request>);
static_assert(std::is_standard_layout_v<ts::abi::pool_config>);
static_assert(std::is_trivially_copyable_v<ts::abi::pool_config>);

static_assert(sizeof(ts::abi::status) == 16);
static_assert(alignof(ts::abi::status) == alignof(std::uint32_t));

namespace
{

struct CallbackState
{
    std::size_t seen = 0;
    bool found_name = false;
    bool found_component = false;
};

struct PoolCallbackState
{
    std::atomic<int> ran{0};
    std::atomic<int> completed{0};
};

void collect_threads(ts::abi::thread_info_view const* info, void* user_data) noexcept
{
    auto& state = *static_cast<CallbackState*>(user_data);
    ++state.seen;
    if (info == nullptr)
        return;

    auto const name = info->name.view();
    auto const component = info->component_tag.view();
    if (name == "abi-worker")
        state.found_name = true;
    if (component == "stable-test")
        state.found_component = true;
}

void run_pool_task(void* user_data) noexcept
{
    auto& state = *static_cast<PoolCallbackState*>(user_data);
    state.ran.fetch_add(1, std::memory_order_relaxed);
}

void complete_pool_task(ts::abi::status status, void* user_data) noexcept
{
    auto& state = *static_cast<PoolCallbackState*>(user_data);
    if (ts::abi::succeeded(status))
        state.completed.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

#if defined(THREADSCHEDULE_RUNTIME)
TEST(StableAbiTest, RuntimeReportsExpectedAbiVersion)
{
    EXPECT_EQ(ts::abi::runtime_abi_version(), ts::abi::abi_version);
}

TEST(StableAbiTest, DefaultAbiStructsCarrySizeAndVersion)
{
    ts::abi::status status{};
    EXPECT_EQ(status.size, sizeof(ts::abi::status));
    EXPECT_EQ(status.version, ts::abi::abi_version);

    ts::abi::thread_info_view info{};
    EXPECT_EQ(info.size, sizeof(ts::abi::thread_info_view));
    EXPECT_EQ(info.version, ts::abi::abi_version);

    ts::abi::pool_config pool{};
    EXPECT_EQ(pool.size, sizeof(ts::abi::pool_config));
    EXPECT_EQ(pool.version, ts::abi::abi_version);
}

TEST(StableAbiTest, InvalidRegistryHandlesReturnErrors)
{
    ts::abi::registry_handle invalid{};

    EXPECT_EQ(ts::abi::registry_count(invalid), 0U);
    EXPECT_EQ(ts::abi::registry_for_each(invalid, &collect_threads).code, ts::abi::status_code::invalid_argument);
    EXPECT_EQ(ts::abi::register_current_thread(invalid, "bad", "bad").code,
              ts::abi::status_code::invalid_argument);
    EXPECT_EQ(ts::abi::unregister_current_thread(invalid).code, ts::abi::status_code::invalid_argument);
    EXPECT_EQ(ts::abi::pool_post({}, &run_pool_task).code, ts::abi::status_code::invalid_argument);
    EXPECT_EQ(ts::abi::pool_wait({}).code, ts::abi::status_code::invalid_argument);
}

TEST(StableAbiTest, RuntimeRegistryRegistrationAndEnumeration)
{
    auto const handle = ts::abi::current_registry();
    auto const before = ts::abi::registry_count(handle);

    {
        ts::abi::AutoRegisterCurrentThread guard("abi-worker", "stable-test");
        ASSERT_TRUE(guard.active());
        EXPECT_EQ(ts::abi::registry_count(handle), before + 1);

        CallbackState state{};
        auto const status = ts::abi::registry_for_each(handle, &collect_threads, &state);
        EXPECT_TRUE(ts::abi::succeeded(status));
        EXPECT_GE(state.seen, before + 1);
        EXPECT_TRUE(state.found_name);
        EXPECT_TRUE(state.found_component);
    }

    EXPECT_EQ(ts::abi::registry_count(handle), before);
}

TEST(StableAbiTest, RuntimePoolPostsWaitsReportsStatsAndShutsDown)
{
    ts::abi::pool_config config{};
    config.worker_count = 2;
    config.worker_scheduling = {};
    config.worker_scheduling.intent = ts::abi::scheduling_intent::normal;

    auto pool = ts::abi::create_pool(config);
    ASSERT_TRUE(pool.valid());

    PoolCallbackState state{};
    EXPECT_TRUE(ts::abi::succeeded(ts::abi::pool_post(pool, &run_pool_task, &state, &complete_pool_task, &state)));
    EXPECT_TRUE(ts::abi::succeeded(ts::abi::pool_wait(pool)));

    ts::abi::pool_stats_view stats{};
    EXPECT_TRUE(ts::abi::succeeded(ts::abi::pool_stats(pool, stats)));
    EXPECT_EQ(state.ran.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(state.completed.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(stats.submitted, 1U);
    EXPECT_EQ(stats.completed, 1U);
    EXPECT_EQ(stats.failed, 0U);
    EXPECT_EQ(stats.worker_count, 2U);

    EXPECT_TRUE(ts::abi::succeeded(ts::abi::pool_shutdown(pool)));
    EXPECT_EQ(ts::abi::pool_post(pool, &run_pool_task, &state).code, ts::abi::status_code::shutdown);
    ts::abi::destroy_pool(pool);
}
#endif
