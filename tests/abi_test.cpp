#include <gtest/gtest.h>

#include <threadschedule/abi.hpp>
#include <threadschedule/thread_registry.hpp>

namespace ts = threadschedule;

static_assert(ts::abi::is_abi_stable_v<ts::abi::registry_handle>);
static_assert(ts::abi::is_abi_stable_v<ts::abi::thread_info_view>);
static_assert(!ts::abi::is_abi_stable_v<ts::ThreadRegistry>);

THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(void, ::threadschedule::abi::registry_handle);

namespace
{

struct CallbackState
{
    std::size_t seen = 0;
    bool found_name = false;
    bool found_component = false;
};

void collect_threads(ts::abi::thread_info_view const* info, void* user_data)
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

} // namespace

#if defined(THREADSCHEDULE_RUNTIME)
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
#endif
