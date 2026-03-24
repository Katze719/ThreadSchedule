#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <threadschedule/registered_threads.hpp>
#include <threadschedule/thread_registry.hpp>
#ifndef _WIN32
#    include <sched.h>
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

TEST(ThreadRegistryTest, ThreadWrapperRegMoveAssign)
{
    std::atomic<bool> ran{false};
    ThreadWrapperReg t;
    EXPECT_FALSE(t.joinable());

    t = ThreadWrapperReg("move-tw", "move", [&] {
        ran = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });

    EXPECT_TRUE(t.joinable());
    t.join();
    EXPECT_TRUE(ran.load());
}

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
TEST(ThreadRegistryTest, JThreadWrapperRegMoveAssign)
{
    std::atomic<bool> ran{false};
    JThreadWrapperReg t;
    EXPECT_FALSE(t.joinable());

    t = JThreadWrapperReg("move-jt", "move", [&](std::stop_token const&) {
        ran = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });

    EXPECT_TRUE(t.joinable());
    t.join();
    EXPECT_TRUE(ran.load());
}

TEST(ThreadRegistryTest, JThreadWrapperRegMoveAssignNoStopToken)
{
    std::atomic<bool> ran{false};
    JThreadWrapperReg t;

    t = JThreadWrapperReg("move-jt-ns", "move", [&] {
        ran = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });

    EXPECT_TRUE(t.joinable());
    t.join();
    EXPECT_TRUE(ran.load());
}

TEST(ThreadRegistryTest, JThreadWrapperRegMemberFunction)
{
    struct Worker
    {
        std::atomic<bool> ran{false};

        void run()
        {
            ran = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        void run_with_stop(std::stop_token st)
        {
            while (!st.stop_requested())
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ran = true;
        }
    };

    Worker w1;
    JThreadWrapperReg t;
    t = JThreadWrapperReg("member-fn", "mf", &Worker::run, &w1);
    t.join();
    EXPECT_TRUE(w1.ran.load());

    Worker w2;
    JThreadWrapperReg t2("member-fn-st", "mf", &Worker::run_with_stop, &w2);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t2.request_stop();
    t2.join();
    EXPECT_TRUE(w2.ran.load());
}

TEST(ThreadRegistryTest, JThreadWrapperRegMoveConstruct)
{
    std::atomic<bool> ran{false};
    JThreadWrapperReg a("mc-jt", "move", [&](std::stop_token const&) {
        ran = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });

    JThreadWrapperReg b = std::move(a);
    EXPECT_FALSE(a.joinable());
    EXPECT_TRUE(b.joinable());
    b.join();
    EXPECT_TRUE(ran.load());
}

TEST(ThreadRegistryTest, JThreadWrapperRegReassign)
{
    std::atomic<int> counter{0};

    JThreadWrapperReg t("r1", "reassign", [&](std::stop_token const&) {
        counter.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
    t.join();

    t = JThreadWrapperReg("r2", "reassign", [&](std::stop_token const&) {
        counter.fetch_add(10);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
    t.join();

    EXPECT_EQ(counter.load(), 11);
}
#endif

TEST(ThreadRegistryTest, CallbackOnUnregisterFires)
{
    registry().unregister_current_thread();

    std::atomic<int> calls{0};
    std::atomic<Tid> lastTid{0};

    registry().set_on_unregister([&](RegisteredThreadInfo const& e) {
        calls.fetch_add(1, std::memory_order_relaxed);
        lastTid.store(e.tid, std::memory_order_relaxed);
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
