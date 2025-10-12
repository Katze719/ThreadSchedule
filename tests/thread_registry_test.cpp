#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <threadschedule/registered_threads.hpp>
#include <threadschedule/thread_registry.hpp>
#ifndef _WIN32
#include <sched.h>
#endif

using namespace threadschedule;

TEST(ThreadRegistryTest, RegistersAndApplies)
{
    std::atomic<bool> ran{false};
    ThreadWrapperReg t("treg", "test", [&] {
        ran = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Find by tag and set a neutral priority
    bool found = false;
    registry().apply(
        [&](RegisteredThreadInfo const& e) {
            found = found || (e.componentTag == "test");
            return e.componentTag == "test";
        },
        [&](RegisteredThreadInfo const& e) { (void)registry().set_priority(e.tid, ThreadPriority{0}); });

    EXPECT_TRUE(found);

    t.join();
    EXPECT_TRUE(ran.load());
}

#ifndef _WIN32
TEST(ThreadRegistryTest, LinuxAffinitySet)
{
    ThreadWrapperReg t("treg2", "aff", [] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    ThreadAffinity aff;
    aff.clear();
    aff.add_cpu(0);

    bool attempted = false;
    registry().apply([](RegisteredThreadInfo const& e) { return e.componentTag == "aff"; },
                     [&](RegisteredThreadInfo const& e) {
                         attempted = true;
                         (void)registry().set_affinity(e.tid, aff);
                     });

    EXPECT_TRUE(attempted);
    t.join();
}
#endif

TEST(ThreadRegistryTest, DuplicateRegistrationIsNoOp)
{
    // Register current thread manually twice and ensure the first registration wins
    // and that count remains 1 and properties are from the first call
    registry().unregister_current_thread();

    AutoRegisterCurrentThread guard1("first-name", "first-comp");

    // Attempt duplicate registration for the same thread id
    registry().register_current_thread(std::string("second-name"), std::string("second-comp"));

    // Snapshot and checks
    auto snapshot = registry().query().entries();
    ASSERT_GE(snapshot.size(), static_cast<size_t>(1));

    // Find this current thread's entry by std::thread::id
    auto selfStdId = std::this_thread::get_id();
    auto it = std::find_if(snapshot.begin(), snapshot.end(),
                           [&](RegisteredThreadInfo const& e) { return e.stdId == selfStdId; });
    ASSERT_TRUE(it != snapshot.end());

    // Expect first registration values to persist
    EXPECT_EQ(it->name, std::string("first-name"));
    EXPECT_EQ(it->componentTag, std::string("first-comp"));
}

TEST(ThreadRegistryTest, CallbackOnRegisterFires)
{
    // Ensure clean state and no side effects from other tests
    registry().unregister_current_thread();

    std::atomic<int> calls{0};
    std::atomic<Tid> lastTid{0};
    std::string lastName;
    std::string lastComp;

    registry().set_on_register([&](RegisteredThreadInfo const& e) {
        calls.fetch_add(1, std::memory_order_relaxed);
        lastTid.store(e.tid, std::memory_order_relaxed);
        lastName = e.name;
        lastComp = e.componentTag;
    });

    {
        AutoRegisterCurrentThread guard("cb-name", "cb-comp");
        EXPECT_GE(calls.load(std::memory_order_relaxed), 1);
        EXPECT_EQ(lastTid.load(std::memory_order_relaxed), ThreadInfo::get_thread_id());
        EXPECT_EQ(lastName, std::string("cb-name"));
        EXPECT_EQ(lastComp, std::string("cb-comp"));
    }

    // Reset hook
    registry().set_on_register({});
}

TEST(ThreadRegistryTest, CallbackOnUnregisterFires)
{
    registry().unregister_current_thread();

    std::atomic<int> calls{0};
    std::atomic<Tid> lastTid{0};

    registry().set_on_unregister([&](Tid tid) {
        calls.fetch_add(1, std::memory_order_relaxed);
        lastTid.store(tid, std::memory_order_relaxed);
    });

    Tid currentTid = 0;
    {
        AutoRegisterCurrentThread guard("cb2-name", "cb2-comp");
        currentTid = ThreadInfo::get_thread_id();
    } // guard dtor should unregister

    EXPECT_GE(calls.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(lastTid.load(std::memory_order_relaxed), currentTid);

    // Reset hook
    registry().set_on_unregister({});
}
