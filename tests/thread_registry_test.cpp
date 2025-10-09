#include <gtest/gtest.h>
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
