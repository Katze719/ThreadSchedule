#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#ifndef _WIN32
#include <sched.h>
#endif
#include <thread>
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

class ThreadWrapperViewTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
    }
    void TearDown() override
    {
    }
};

TEST_F(ThreadWrapperViewTest, WrapExistingStdThreadAndSetName)
{
    std::thread t([] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });

    ThreadWrapperView view(t);
    auto r = view.set_name("view_thread");
    (void)r;

    EXPECT_TRUE(view.joinable());
    view.join();
    EXPECT_FALSE(view.joinable());
}

TEST_F(ThreadWrapperViewTest, ViewDoesNotOwnLifetime)
{
    std::atomic<bool> ran{false};
    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ran = true;
    });

    {
        ThreadWrapperView view(t);
        EXPECT_TRUE(view.joinable());
    } // view destroyed, thread still running/owned by t

    t.join();
    EXPECT_TRUE(ran);
}

#if __cplusplus >= 202002L
TEST_F(ThreadWrapperViewTest, WrapExistingJThreadAndStop)
{
    std::atomic<int> ticks{0};
    std::jthread jt([&](std::stop_token st) {
        while (!st.stop_requested())
        {
            ticks++;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    JThreadWrapperView jview(jt);
    EXPECT_FALSE(jview.stop_requested());
    jview.request_stop();
    jview.join();
    EXPECT_GE(ticks.load(), 0);
}
#endif

#ifndef _WIN32
TEST_F(ThreadWrapperViewTest, ThreadByNameViewSetName)
{
    // Start a thread and set an initial name via owning wrapper
    std::thread t([] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
    ThreadWrapperView view(t);
    ASSERT_TRUE(view.set_name("th_1").has_value());

    // Find it by name and rename via ThreadByNameView
    ThreadByNameView by_name("th_1");
    ASSERT_TRUE(by_name.found());
    EXPECT_TRUE(by_name.get_name().has_value());
    ASSERT_TRUE(by_name.set_name("new_name").has_value());

    // Verify new name
    ThreadByNameView verify("new_name");
    ASSERT_TRUE(verify.found());
    auto nm = verify.get_name();
    ASSERT_TRUE(nm.has_value());
    EXPECT_EQ(nm.value(), "new_name");

    view.join();
}
#endif

#ifndef _WIN32
TEST_F(ThreadWrapperViewTest, ThreadByNameBindToCpu0)
{
    // Start a thread and name it
    std::thread t([] { std::this_thread::sleep_for(std::chrono::milliseconds(200)); });
    ThreadWrapperView view(t);
    ASSERT_TRUE(view.set_name("th_bind").has_value());

    // Lookup by name and bind to CPU 0
    ThreadByNameView by_name("th_bind");
    ASSERT_TRUE(by_name.found());

    ThreadAffinity affinity;
    affinity.add_cpu(0);
    auto res = by_name.set_affinity(affinity);
    if (!res.has_value())
    {
        GTEST_SKIP() << "set_affinity not permitted: " << res.error().message();
    }

    // Verify via sched_getaffinity on the TID
    cpu_set_t mask;
    CPU_ZERO(&mask);
    int rc = sched_getaffinity(by_name.native_handle(), sizeof(mask), &mask);
    ASSERT_EQ(rc, 0) << "sched_getaffinity failed";
    EXPECT_TRUE(CPU_ISSET(0, &mask));

    view.join();
}
#endif
