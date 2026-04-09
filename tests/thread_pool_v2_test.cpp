#include <atomic>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <threadschedule/threadschedule.hpp>
#include <vector>

using namespace threadschedule;

// ==================== try_submit / try_post ====================

TEST(PoolV2, TrySubmitReturnsExpected)
{
    ThreadPool pool(2);
    auto result = pool.try_submit([] { return 42; });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().get(), 42);
}

TEST(PoolV2, TrySubmitAfterShutdownReturnsError)
{
    ThreadPool pool(2);
    pool.shutdown();
    auto result = pool.try_submit([] { return 1; });
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), std::make_error_code(std::errc::operation_canceled));
}

TEST(PoolV2, TryPostReturnsExpected)
{
    ThreadPool pool(2);
    std::atomic<bool> ran{false};
    auto result = pool.try_post([&ran] { ran = true; });
    ASSERT_TRUE(result.has_value());
    pool.wait_for_tasks();
    EXPECT_TRUE(ran);
}

TEST(PoolV2, TryPostAfterShutdownReturnsError)
{
    ThreadPool pool(2);
    pool.shutdown();
    auto result = pool.try_post([] {});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), std::make_error_code(std::errc::operation_canceled));
}

TEST(PoolV2, PostThrowsOnShutdown)
{
    ThreadPool pool(2);
    pool.shutdown();
    EXPECT_THROW(pool.post([] {}), std::runtime_error);
}

// ==================== submit_batch / try_submit_batch ====================

TEST(PoolV2, SubmitBatchExecutesAll)
{
    HighPerformancePool pool(4);
    std::atomic<int> count{0};
    std::vector<std::function<void()>> tasks;
    for (int i = 0; i < 100; ++i)
        tasks.push_back([&count] { count.fetch_add(1, std::memory_order_relaxed); });

    auto futures = pool.submit_batch(tasks.begin(), tasks.end());
    for (auto& f : futures)
        f.get();

    EXPECT_EQ(count.load(), 100);
}

TEST(PoolV2, TrySubmitBatchAfterShutdown)
{
    HighPerformancePool pool(2);
    pool.shutdown();
    std::vector<std::function<void()>> tasks = {[] {}, [] {}};
    auto result = pool.try_submit_batch(tasks.begin(), tasks.end());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), std::make_error_code(std::errc::operation_canceled));
}

TEST(PoolV2, SubmitBatchOnThreadPool)
{
    ThreadPool pool(2);
    std::atomic<int> count{0};
    std::vector<std::function<void()>> tasks;
    for (int i = 0; i < 50; ++i)
        tasks.push_back([&count] { count.fetch_add(1, std::memory_order_relaxed); });

    auto futures = pool.submit_batch(tasks.begin(), tasks.end());
    for (auto& f : futures)
        f.get();

    EXPECT_EQ(count.load(), 50);
}

// ==================== parallel_for_each ====================

TEST(PoolV2, ParallelForEachHP)
{
    HighPerformancePool pool(4);
    std::vector<std::atomic<int>> values(100);
    for (auto& v : values)
        v.store(0);

    std::vector<int> indices(100);
    std::iota(indices.begin(), indices.end(), 0);

    pool.parallel_for_each(indices.begin(), indices.end(), [&values](int idx) { values[idx].store(idx * 2); });

    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(values[i].load(), i * 2);
}

TEST(PoolV2, ParallelForEachThreadPool)
{
    ThreadPool pool(2);
    std::vector<int> data(50, 1);

    pool.parallel_for_each(data.begin(), data.end(), [](int& v) { v *= 3; });

    for (auto const& v : data)
        EXPECT_EQ(v, 3);
}

// ==================== ShutdownPolicy ====================

TEST(PoolV2, ShutdownDrainCompletesAllTasks)
{
    std::atomic<int> count{0};
    {
        ThreadPool pool(2);
        for (int i = 0; i < 20; ++i)
            pool.post([&count] {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                count.fetch_add(1, std::memory_order_relaxed);
            });
        pool.shutdown(ShutdownPolicy::drain);
    }
    EXPECT_EQ(count.load(), 20);
}

TEST(PoolV2, ShutdownDropPendingMaySkipTasks)
{
    std::atomic<int> count{0};
    {
        HighPerformancePool pool(1);
        pool.post([&count] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            count.fetch_add(1, std::memory_order_relaxed);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        for (int i = 0; i < 100; ++i)
            pool.post([&count] { count.fetch_add(1, std::memory_order_relaxed); });
        pool.shutdown(ShutdownPolicy::drop_pending);
    }
    EXPECT_LT(count.load(), 101);
}

TEST(PoolV2, ShutdownForTimedDrain)
{
    ThreadPool pool(2);
    std::atomic<int> count{0};
    for (int i = 0; i < 5; ++i)
        pool.post([&count] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            count.fetch_add(1, std::memory_order_relaxed);
        });

    bool drained = pool.shutdown_for(std::chrono::milliseconds(5000));
    EXPECT_TRUE(drained);
    EXPECT_EQ(count.load(), 5);
}

// ==================== post on all pool types ====================

TEST(PoolV2, HPPoolPost)
{
    HighPerformancePool pool(2);
    std::atomic<bool> ran{false};
    pool.post([&ran] { ran = true; });
    pool.wait_for_tasks();
    EXPECT_TRUE(ran);
}

TEST(PoolV2, FastPoolPost)
{
    FastThreadPool pool(2);
    std::atomic<bool> ran{false};
    pool.post([&ran] { ran = true; });
    pool.wait_for_tasks();
    EXPECT_TRUE(ran);
}

// ==================== HP Pool deque_capacity constructor ====================

TEST(PoolV2, HPPoolCustomDequeCapacity)
{
    HighPerformancePool pool(2, 64);
    auto f = pool.submit([] { return 7; });
    EXPECT_EQ(f.get(), 7);
}

// ==================== Trace callbacks ====================

TEST(PoolV2, TraceCallbacksHP)
{
    HighPerformancePool pool(2);
    std::atomic<int> starts{0};
    std::atomic<int> ends{0};

    pool.set_on_task_start([&starts](auto, auto) { starts.fetch_add(1, std::memory_order_relaxed); });
    pool.set_on_task_end([&ends](auto, auto, auto) { ends.fetch_add(1, std::memory_order_relaxed); });

    for (int i = 0; i < 10; ++i)
        pool.post([] {});

    pool.wait_for_tasks();
    EXPECT_EQ(starts.load(), 10);
    EXPECT_EQ(ends.load(), 10);
}

TEST(PoolV2, TraceCallbacksThreadPool)
{
    ThreadPool pool(2);
    std::atomic<int> starts{0};
    std::atomic<int> ends{0};

    pool.set_on_task_start([&starts](auto, auto) { starts.fetch_add(1, std::memory_order_relaxed); });
    pool.set_on_task_end([&ends](auto, auto, auto) { ends.fetch_add(1, std::memory_order_relaxed); });

    for (int i = 0; i < 10; ++i)
        pool.post([] {});

    pool.wait_for_tasks();
    EXPECT_EQ(starts.load(), 10);
    EXPECT_EQ(ends.load(), 10);
}

// ==================== LightweightPool ====================

TEST(PoolV2, LightweightPoolPost)
{
    LightweightPool pool(2);
    std::atomic<int> count{0};

    for (int i = 0; i < 50; ++i)
        pool.post([&count] { count.fetch_add(1, std::memory_order_relaxed); });

    pool.shutdown(ShutdownPolicy::drain);
    EXPECT_EQ(count.load(), 50);
}

TEST(PoolV2, LightweightPoolTryPost)
{
    LightweightPool pool(2);
    std::atomic<bool> ran{false};
    auto result = pool.try_post([&ran] { ran = true; });
    ASSERT_TRUE(result.has_value());
    pool.shutdown(ShutdownPolicy::drain);
    EXPECT_TRUE(ran);
}

TEST(PoolV2, LightweightPoolPostBatch)
{
    LightweightPool pool(4);
    std::atomic<int> count{0};
    std::vector<std::function<void()>> tasks;
    for (int i = 0; i < 100; ++i)
        tasks.push_back([&count] { count.fetch_add(1, std::memory_order_relaxed); });

    pool.post_batch(tasks.begin(), tasks.end());
    pool.shutdown(ShutdownPolicy::drain);
    EXPECT_EQ(count.load(), 100);
}

TEST(PoolV2, LightweightPoolShutdownDropPending)
{
    std::atomic<int> count{0};
    {
        LightweightPool pool(1);
        pool.post([&count] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            count.fetch_add(1, std::memory_order_relaxed);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        for (int i = 0; i < 50; ++i)
            pool.post([&count] { count.fetch_add(1, std::memory_order_relaxed); });
        pool.shutdown(ShutdownPolicy::drop_pending);
    }
    EXPECT_LT(count.load(), 51);
}

TEST(PoolV2, LightweightPoolShutdownFor)
{
    LightweightPool pool(2);
    std::atomic<int> count{0};
    for (int i = 0; i < 5; ++i)
        pool.post([&count] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            count.fetch_add(1, std::memory_order_relaxed);
        });
    bool drained = pool.shutdown_for(std::chrono::milliseconds(5000));
    EXPECT_TRUE(drained);
    EXPECT_EQ(count.load(), 5);
}

TEST(PoolV2, LightweightPoolConfigureThreads)
{
    LightweightPool pool(2);
    auto r = pool.configure_threads("lite");
    EXPECT_TRUE(r.has_value());
}

TEST(PoolV2, LightweightPoolCustomTaskSize)
{
    LightweightPoolT<128> pool(2);
    std::atomic<bool> ran{false};
    pool.post([&ran] { ran = true; });
    pool.shutdown(ShutdownPolicy::drain);
    EXPECT_TRUE(ran);
}

// ==================== GlobalPool ====================

TEST(PoolV2, GlobalThreadPoolSubmit)
{
    auto f = GlobalThreadPool::submit([] { return 99; });
    EXPECT_EQ(f.get(), 99);
}

TEST(PoolV2, GlobalThreadPoolPost)
{
    std::atomic<bool> ran{false};
    GlobalThreadPool::post([&ran] { ran = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(ran);
}

// ==================== ScheduledThreadPool ====================

TEST(PoolV2, ScheduledAfterBasic)
{
    ScheduledThreadPool scheduler(2);
    std::atomic<bool> ran{false};

    scheduler.schedule_after(std::chrono::milliseconds(50), [&ran] { ran = true; });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(ran);
}

TEST(PoolV2, ScheduledPeriodicRunsMultipleTimes)
{
    ScheduledThreadPool scheduler(2);
    std::atomic<int> count{0};

    auto handle = scheduler.schedule_periodic(std::chrono::milliseconds(30), [&count] {
        count.fetch_add(1, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    handle.cancel();
    scheduler.shutdown();
    EXPECT_GE(count.load(), 3);
}

TEST(PoolV2, ScheduledCancel)
{
    ScheduledThreadPool scheduler(2);
    std::atomic<bool> ran{false};

    auto handle = scheduler.schedule_after(std::chrono::milliseconds(200), [&ran] { ran = true; });
    handle.cancel();
    EXPECT_TRUE(handle.is_cancelled());

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_FALSE(ran);
}

TEST(PoolV2, ScheduledInsertAfterShutdownReturnsCancelledHandle)
{
    ScheduledThreadPool scheduler(2);
    scheduler.shutdown();

    auto handle = scheduler.schedule_after(std::chrono::milliseconds(10), [] {});
    EXPECT_TRUE(handle.is_cancelled());
}

TEST(PoolV2, ScheduledHPPool)
{
    ScheduledHighPerformancePool scheduler(2);
    std::atomic<bool> ran{false};
    scheduler.schedule_after(std::chrono::milliseconds(20), [&ran] { ran = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(ran);
}

TEST(PoolV2, ScheduledLightweight)
{
    ScheduledLightweightPool scheduler(2);
    std::atomic<bool> ran{false};
    scheduler.schedule_after(std::chrono::milliseconds(20), [&ran] { ran = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(ran);
}

// ==================== InlinePool ====================

TEST(PoolV2, InlinePoolSubmit)
{
    InlinePool pool;
    auto f = pool.submit([] { return 42; });
    EXPECT_EQ(f.get(), 42);
}

TEST(PoolV2, InlinePoolPost)
{
    InlinePool pool;
    int value = 0;
    pool.post([&value] { value = 7; });
    EXPECT_EQ(value, 7);
}

TEST(PoolV2, InlinePoolExceptionPropagation)
{
    InlinePool pool;
    auto f = pool.submit([]() -> int { throw std::runtime_error("inline boom"); });
    EXPECT_THROW(f.get(), std::runtime_error);
}

TEST(PoolV2, InlinePoolShutdown)
{
    InlinePool pool;
    pool.shutdown();
    EXPECT_THROW(pool.submit([] { return 1; }), std::runtime_error);
}

TEST(PoolV2, InlinePoolParallelForEach)
{
    InlinePool pool;
    std::vector<int> data = {1, 2, 3, 4, 5};
    pool.parallel_for_each(data.begin(), data.end(), [](int& v) { v *= 10; });
    EXPECT_EQ(data, (std::vector<int>{10, 20, 30, 40, 50}));
}

// ==================== task_group ====================

TEST(PoolV2, TaskGroupWaitsForAll)
{
    ThreadPool pool(2);
    std::atomic<int> count{0};
    {
        task_group<ThreadPool> group(pool);
        for (int i = 0; i < 10; ++i)
            group.submit([&count] { count.fetch_add(1, std::memory_order_relaxed); });
        group.wait();
    }
    EXPECT_EQ(count.load(), 10);
}

TEST(PoolV2, TaskGroupDestructorWaits)
{
    ThreadPool pool(2);
    std::atomic<int> count{0};
    {
        task_group<ThreadPool> group(pool);
        for (int i = 0; i < 5; ++i)
            group.submit([&count] {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                count.fetch_add(1, std::memory_order_relaxed);
            });
    }
    EXPECT_EQ(count.load(), 5);
}

TEST(PoolV2, TaskGroupPropagatesException)
{
    ThreadPool pool(2);
    task_group<ThreadPool> group(pool);
    group.submit([] { throw std::runtime_error("group fail"); });
    EXPECT_THROW(group.wait(), std::runtime_error);
}

TEST(PoolV2, TaskGroupPendingCount)
{
    ThreadPool pool(2);
    task_group<ThreadPool> group(pool);
    EXPECT_EQ(group.pending(), 0u);
    group.submit([] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
    EXPECT_GE(group.pending(), 0u);
    group.wait();
    EXPECT_EQ(group.pending(), 0u);
}

TEST(PoolV2, TaskGroupWithInlinePool)
{
    InlinePool pool;
    int sum = 0;
    {
        task_group<InlinePool> group(pool);
        group.submit([&sum] { sum += 1; });
        group.submit([&sum] { sum += 2; });
        group.submit([&sum] { sum += 3; });
        group.wait();
    }
    EXPECT_EQ(sum, 6);
}

#if __cpp_lib_jthread >= 201911L
// ==================== Stop-token tasks (C++20) ====================

TEST(PoolV2, SubmitWithStopTokenSkipsWhenStopped)
{
    ThreadPool pool(2);
    std::stop_source src;
    src.request_stop();

    auto f = pool.submit(src.get_token(), [] { return 42; });
    EXPECT_EQ(f.get(), 0);
}

TEST(PoolV2, SubmitWithStopTokenExecutesNormally)
{
    ThreadPool pool(2);
    std::stop_source src;

    auto f = pool.submit(src.get_token(), [] { return 42; });
    EXPECT_EQ(f.get(), 42);
}

TEST(PoolV2, TrySubmitWithStopToken)
{
    HighPerformancePool pool(2);
    std::stop_source src;
    auto result = pool.try_submit(src.get_token(), [] { return 7; });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().get(), 7);
}
#endif
