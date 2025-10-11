#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

class ThreadWrapperTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Setup code if needed
    }

    void TearDown() override
    {
        // Cleanup code if needed
    }
};

// Test basic thread creation and execution
TEST_F(ThreadWrapperTest, BasicThreadCreation)
{
    std::atomic<bool> executed{false};

    ThreadWrapper thread([&executed]() { executed = true; });

    EXPECT_TRUE(thread.joinable());
    thread.join();
    EXPECT_FALSE(thread.joinable());
    EXPECT_TRUE(executed);
}

namespace
{
void take_std_thread(std::thread t)
{
    t.join();
}

void take_thread_wrapper(ThreadWrapper w)
{
    w.join();
}
#if __cplusplus >= 202002L
void take_std_jthread(std::jthread jt)
{
    jt.join();
}

void take_jthread_wrapper(JThreadWrapper jw)
{
    jw.join();
}
#endif
} // namespace

TEST_F(ThreadWrapperTest, ConvertWrapperToStdThreadViaRelease)
{
    std::atomic<bool> executed{false};
    ThreadWrapper w([&] { executed = true; });
    take_std_thread(w.release());
    EXPECT_TRUE(executed);
}

TEST_F(ThreadWrapperTest, ConvertWrapperToStdThreadViaMoveCast)
{
    std::atomic<bool> executed{false};
    ThreadWrapper w([&] { executed = true; });
    take_std_thread(static_cast<std::thread>(std::move(w)));
    EXPECT_TRUE(executed);
}

TEST_F(ThreadWrapperTest, ConvertStdThreadRvalueToWrapperImplicit)
{
    std::atomic<bool> executed{false};
    auto make_thread = [&]() { return std::thread([&] { executed = true; }); };
    take_thread_wrapper(make_thread());
    EXPECT_TRUE(executed);
}

TEST_F(ThreadWrapperTest, ConvertStdThreadMovedToWrapperImplicit)
{
    std::atomic<bool> executed{false};
    std::thread t([&] { executed = true; });
    take_thread_wrapper(std::move(t));
    EXPECT_TRUE(executed);
}

#if __cplusplus >= 202002L
TEST_F(ThreadWrapperTest, ConvertJWrapperToStdJthreadViaRelease)
{
    std::atomic<bool> executed{false};
    JThreadWrapper jw([&](std::stop_token) { executed = true; });
    take_std_jthread(jw.release());
    EXPECT_TRUE(executed);
}

TEST_F(ThreadWrapperTest, ConvertJWrapperToStdJthreadViaMoveCast)
{
    std::atomic<bool> executed{false};
    JThreadWrapper jw([&](std::stop_token) { executed = true; });
    take_std_jthread(static_cast<std::jthread>(std::move(jw)));
    EXPECT_TRUE(executed);
}

TEST_F(ThreadWrapperTest, ConvertStdJthreadRvalueToJWrapperImplicit)
{
    std::atomic<bool> executed{false};
    auto make_jthread = [&]() { return std::jthread([&](std::stop_token) { executed = true; }); };
    take_jthread_wrapper(make_jthread());
    EXPECT_TRUE(executed);
}

TEST_F(ThreadWrapperTest, ConvertStdJthreadMovedToJWrapperImplicit)
{
    std::atomic<bool> executed{false};
    std::jthread jt([&](std::stop_token) { executed = true; });
    take_jthread_wrapper(std::move(jt));
    EXPECT_TRUE(executed);
}
#endif

// Test thread with parameters
TEST_F(ThreadWrapperTest, ThreadWithParameters)
{
    std::atomic<int> result{0};

    ThreadWrapper thread([&result](int a, int b) { result = a + b; }, 10, 20);

    thread.join();
    EXPECT_EQ(result, 30);
}

// Test thread with return value via promise/future pattern
TEST_F(ThreadWrapperTest, ThreadWithReturnValue)
{
    std::promise<int> promise;
    auto future = promise.get_future();

    ThreadWrapper thread([&promise]() { promise.set_value(42); });

    thread.join();
    EXPECT_EQ(future.get(), 42);
}

// Test thread naming (Windows 10+)
TEST_F(ThreadWrapperTest, ThreadNaming)
{
    ThreadWrapper thread([]() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

    auto set_name_result = thread.set_name("test_thread");
    bool const name_set = set_name_result.has_value();

#ifdef _WIN32
    // On Windows, naming might fail if not Windows 10+
    // We just check that the function doesn't crash
    if (name_set)
    {
        auto name = thread.get_name();
        EXPECT_TRUE(name.has_value());
        if (name.has_value())
        {
            EXPECT_EQ(name.value(), "test_thread");
        }
    }
#else
    // On Linux, naming should work
    EXPECT_TRUE(name_set);
    auto name = thread.get_name();
    EXPECT_TRUE(name.has_value());
    if (name.has_value())
    {
        EXPECT_EQ(name.value(), "test_thread");
    }
#endif

    thread.join();
}

#ifndef _WIN32
// POSIX: long names (>15) should fail with invalid_argument
TEST_F(ThreadWrapperTest, ThreadNamingTooLongFails)
{
    ThreadWrapper thread([]() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    std::string long_name(16, 'x');
    auto res = thread.set_name(long_name);
    EXPECT_FALSE(res.has_value());
    if (!res.has_value())
    {
        EXPECT_EQ(res.error(), std::make_error_code(std::errc::invalid_argument));
    }
    thread.join();
}
#endif

// Test thread priority
TEST_F(ThreadWrapperTest, ThreadPriority)
{
    ThreadWrapper thread([]() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

    // Set priority should not crash
    [[maybe_unused]] bool priority_set = thread.set_priority(ThreadPriority::normal()).has_value();
    // Priority setting may fail depending on permissions
    // Just ensure it doesn't crash

    thread.join();
}

// Test thread detach
TEST_F(ThreadWrapperTest, ThreadDetach)
{
    std::atomic<bool> executed{false};

    ThreadWrapper thread([&executed]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        executed = true;
    });

    EXPECT_TRUE(thread.joinable());
    thread.detach();
    EXPECT_FALSE(thread.joinable());

    // Wait a bit to ensure thread completes
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(executed);
}

// Test multiple threads
TEST_F(ThreadWrapperTest, MultipleThreads)
{
    constexpr int num_threads = 10;
    std::atomic<int> counter{0};
    std::vector<std::unique_ptr<ThreadWrapper>> threads;

    for (int i = 0; i < num_threads; ++i)
    {
        threads.push_back(std::make_unique<ThreadWrapper>([&counter]() { counter++; }));
    }

    for (auto& thread : threads)
    {
        thread->join();
    }

    EXPECT_EQ(counter, num_threads);
}

// Test thread exception handling
TEST_F(ThreadWrapperTest, ThreadExceptionHandling)
{
    // Thread exceptions are caught by the thread wrapper implementation
    // This test ensures the thread can be joined even if it throws
    std::atomic<bool> thread_started{false};

    ThreadWrapper thread([&thread_started]() {
        thread_started = true;
        // Exception will be caught internally by std::thread
        try
        {
            throw std::runtime_error("Test exception");
        }
        catch (...)
        {
            // Caught internally
        }
    });

    // Join should complete
    EXPECT_NO_THROW(thread.join());
    EXPECT_TRUE(thread_started);
}

// Test move semantics
TEST_F(ThreadWrapperTest, MoveSemantics)
{
    std::atomic<bool> executed{false};

    ThreadWrapper thread1([&executed]() { executed = true; });

    // Move thread1 to thread2
    ThreadWrapper thread2(std::move(thread1));

    EXPECT_FALSE(thread1.joinable());
    EXPECT_TRUE(thread2.joinable());

    thread2.join();
    EXPECT_TRUE(executed);
}

// Test thread affinity (if supported)
TEST_F(ThreadWrapperTest, ThreadAffinity)
{
    ThreadWrapper thread([]() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

    // Try to set affinity to CPU 0
    ThreadAffinity affinity;
    affinity.add_cpu(0);

    [[maybe_unused]] auto affinity_result = thread.set_affinity(affinity);
    // Affinity setting may fail depending on system/permissions
    // Just ensure it doesn't crash

    thread.join();
}

// Test get_id
TEST_F(ThreadWrapperTest, GetThreadId)
{
    std::thread::id thread_id;

    ThreadWrapper thread([&thread_id]() { thread_id = std::this_thread::get_id(); });

    auto wrapper_id = thread.get_id();
    thread.join();

    EXPECT_NE(wrapper_id, std::thread::id());
    EXPECT_EQ(wrapper_id, thread_id);
}

#if __cplusplus >= 202002L
// C++20 JThreadWrapper tests
TEST_F(ThreadWrapperTest, JThreadWrapperBasic)
{
    std::atomic<bool> executed{false};

    JThreadWrapper jthread([&executed]() { executed = true; });

    jthread.join();
    EXPECT_TRUE(executed);
}

TEST_F(ThreadWrapperTest, JThreadWrapperStopToken)
{
    std::atomic<int> counter{0};

    JThreadWrapper jthread([&counter](std::stop_token st) {
        while (!st.stop_requested())
        {
            counter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    jthread.request_stop();
    jthread.join();

    EXPECT_GT(counter, 0);
}
#endif

// Performance test - thread creation overhead
TEST_F(ThreadWrapperTest, ThreadCreationPerformance)
{
    constexpr int num_iterations = 100;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_iterations; ++i)
    {
        ThreadWrapper thread([]() {});
        thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // Just ensure it completes in reasonable time (< 1 second for 100 threads)
    EXPECT_LT(duration.count(), 1000000);

    std::cout << "Thread creation avg: " << (duration.count() / num_iterations) << " μs/thread" << std::endl;
}
