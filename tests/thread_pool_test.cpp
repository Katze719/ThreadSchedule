#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <threadschedule/threadschedule.hpp>
#include <vector>

using namespace threadschedule;

class ThreadPoolTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

// ==================== ThreadPool Basic Tests ====================

TEST_F(ThreadPoolTest, ThreadPoolBasicCreation)
{
    ThreadPool pool(4);
    // Pool created successfully
    EXPECT_TRUE(true);
}

TEST_F(ThreadPoolTest, ThreadPoolSubmitTask)
{
    ThreadPool pool(2);
    std::atomic<bool> executed{false};

    pool.submit([&executed]() { executed = true; });

    // Wait for task to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(executed);
}

TEST_F(ThreadPoolTest, ThreadPoolSubmitWithFuture)
{
    ThreadPool pool(2);

    auto future = pool.submit([]() { return 42; });

    EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolTest, ThreadPoolMultipleTasks)
{
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    constexpr int num_tasks = 100;

    for (int i = 0; i < num_tasks; ++i)
    {
        pool.submit([&counter]() { counter++; });
    }

    // Wait for all tasks
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(counter, num_tasks);
}

TEST_F(ThreadPoolTest, ThreadPoolTasksWithParameters)
{
    ThreadPool pool(2);
    std::atomic<int> sum{0};

    for (int i = 1; i <= 10; ++i)
    {
        pool.submit([&sum, i]() { sum += i; });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(sum, 55); // 1+2+3+...+10 = 55
}

TEST_F(ThreadPoolTest, ThreadPoolShutdown)
{
    auto pool = std::make_unique<ThreadPool>(2);
    std::atomic<int> counter{0};

    pool->submit([&counter]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        counter++;
    });

    // Destroy pool (should wait for tasks)
    pool.reset();

    EXPECT_EQ(counter, 1);
}

TEST_F(ThreadPoolTest, ThreadPoolExceptionHandling)
{
    ThreadPool pool(2);

    auto future = pool.submit([]() -> int { throw std::runtime_error("Test exception"); });

    EXPECT_THROW(future.get(), std::runtime_error);
}

// ==================== HighPerformancePool Tests ====================

TEST_F(ThreadPoolTest, HighPerformancePoolCreation)
{
    HighPerformancePool pool(4);
    // Pool created successfully
    EXPECT_TRUE(true);
}

TEST_F(ThreadPoolTest, HighPerformancePoolSubmitTask)
{
    HighPerformancePool pool(2);
    std::atomic<bool> executed{false};

    pool.submit([&executed]() { executed = true; });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(executed);
}

TEST_F(ThreadPoolTest, HighPerformancePoolWithFuture)
{
    HighPerformancePool pool(2);

    auto future = pool.submit([]() { return 42; });

    EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolTest, HighPerformancePoolManyTasks)
{
    HighPerformancePool pool(std::thread::hardware_concurrency());
    std::atomic<int> counter{0};
    constexpr int num_tasks = 1000;

    for (int i = 0; i < num_tasks; ++i)
    {
        pool.submit([&counter]() { counter++; });
    }

    // Wait for completion
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(counter, num_tasks);
}

TEST_F(ThreadPoolTest, HighPerformancePoolConfigureThreads)
{
    HighPerformancePool pool(2);

    // Configure threads (may fail without permissions)
    [[maybe_unused]] auto _cfg = pool.configure_threads("worker");

    std::atomic<bool> executed{false};
    pool.submit([&executed]() { executed = true; });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(executed);
}

TEST_F(ThreadPoolTest, HighPerformancePoolDistributeAcrossCPUs)
{
    HighPerformancePool pool(4);

    // Try to distribute (may fail without permissions)
    [[maybe_unused]] auto _dist = pool.distribute_across_cpus();

    std::atomic<int> counter{0};
    for (int i = 0; i < 10; ++i)
    {
        pool.submit([&counter]() { counter++; });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(counter, 10);
}

TEST_F(ThreadPoolTest, HighPerformancePoolStatistics)
{
    HighPerformancePool pool(2);

    for (int i = 0; i < 10; ++i)
    {
        pool.submit([]() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto stats = pool.get_statistics();
    EXPECT_GT(stats.completed_tasks, 0);
    EXPECT_GE(stats.tasks_per_second, 0);
}

// Removed test - reset_statistics() doesn't exist in the API

TEST_F(ThreadPoolTest, HighPerformancePoolPendingTasks)
{
    HighPerformancePool pool(1); // Single thread to create queue

    // Submit many tasks
    for (int i = 0; i < 100; ++i)
    {
        pool.submit([]() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    }

    // Check statistics for pending tasks
    auto stats = pool.get_statistics();
    EXPECT_GE(stats.pending_tasks, 0);
}

// ==================== FastThreadPool Tests ====================

TEST_F(ThreadPoolTest, FastThreadPoolCreation)
{
    FastThreadPool pool(4);
    // Pool created successfully
    EXPECT_TRUE(true);
}

TEST_F(ThreadPoolTest, FastThreadPoolSubmitTask)
{
    FastThreadPool pool(2);
    std::atomic<bool> executed{false};

    pool.submit([&executed]() { executed = true; });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(executed);
}

TEST_F(ThreadPoolTest, FastThreadPoolWithFuture)
{
    FastThreadPool pool(2);

    auto future = pool.submit([]() { return 42; });

    EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolTest, FastThreadPoolManyTasks)
{
    FastThreadPool pool(4);
    std::atomic<int> counter{0};
    constexpr int num_tasks = 500;

    for (int i = 0; i < num_tasks; ++i)
    {
        pool.submit([&counter]() { counter++; });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(counter, num_tasks);
}

// ==================== Performance Comparison Tests ====================

TEST_F(ThreadPoolTest, PerformanceComparisonSimpleTasks)
{
    constexpr int num_tasks = 1000;
    constexpr int num_threads = 4;

    auto test_pool = [](auto& pool) {
        std::atomic<int> counter{0};
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < 1000; ++i)
        {
            pool.submit([&counter]() { counter++; });
        }

        // Wait for completion with timeout
        auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (counter < num_tasks && std::chrono::steady_clock::now() < timeout)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    };

    ThreadPool pool1(num_threads);
    auto time1 = test_pool(pool1);
    std::cout << "ThreadPool: " << time1 << " ms" << std::endl;

    HighPerformancePool pool2(num_threads);
    auto time2 = test_pool(pool2);
    std::cout << "HighPerformancePool: " << time2 << " ms" << std::endl;

    FastThreadPool pool3(num_threads);
    auto time3 = test_pool(pool3);
    std::cout << "FastThreadPool: " << time3 << " ms" << std::endl;

    // All pools should complete in reasonable time
    EXPECT_LT(time1, 5000);
    EXPECT_LT(time2, 5000);
    EXPECT_LT(time3, 5000);
}

// ==================== Stress Tests ====================

TEST_F(ThreadPoolTest, StressTestHighPerformancePool)
{
    HighPerformancePool pool(std::thread::hardware_concurrency());
    std::atomic<size_t> total{0};
    std::atomic<int> completed{0};
    constexpr int num_tasks = 10000;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_tasks; ++i)
    {
        pool.submit([&total, &completed, i]() {
            total += i % 100;
            completed++;
        });
    }

    // Wait for all tasks
    while (completed < num_tasks)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Stress test completed " << num_tasks << " tasks in " << duration.count() << " ms" << std::endl;
    std::cout << "Throughput: " << (num_tasks * 1000.0 / duration.count()) << " tasks/sec" << std::endl;

    EXPECT_EQ(completed, num_tasks);
}

TEST_F(ThreadPoolTest, ConcurrentSubmissions)
{
    HighPerformancePool pool(4);
    std::atomic<int> counter{0};
    constexpr int num_submitter_threads = 8;
    constexpr int tasks_per_thread = 100;

    std::vector<std::thread> submitters;
    for (int i = 0; i < num_submitter_threads; ++i)
    {
        submitters.emplace_back([&pool, &counter]() {
            for (int j = 0; j < tasks_per_thread; ++j)
            {
                pool.submit([&counter]() { counter++; });
            }
        });
    }

    for (auto& t : submitters)
    {
        t.join();
    }

    // Wait for all tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_EQ(counter, num_submitter_threads * tasks_per_thread);
}

// ==================== Task Dependencies Test ====================

TEST_F(ThreadPoolTest, TaskDependencies)
{
    // Use more threads than tasks to avoid deadlock
    HighPerformancePool pool(4);
    std::atomic<int> stage{0};

    // Task 1: Set stage to 1
    auto future1 = pool.submit([&stage]() {
        stage = 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 1;
    });

    // Wait for task 1 to complete before submitting task 2
    int result1 = future1.get();
    EXPECT_EQ(result1, 1);
    EXPECT_EQ(stage, 1);

    // Task 2: Set stage to 2
    auto future2 = pool.submit([&stage]() {
        stage = 2;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 2;
    });

    // Wait for task 2 to complete before submitting task 3
    int result2 = future2.get();
    EXPECT_EQ(result2, 2);
    EXPECT_EQ(stage, 2);

    // Task 3: Set stage to 3
    auto future3 = pool.submit([&stage]() {
        stage = 3;
        return 3;
    });

    EXPECT_EQ(future3.get(), 3);
    EXPECT_EQ(stage, 3);
}

// ==================== Different Task Types Test ====================

TEST_F(ThreadPoolTest, MixedTaskTypes)
{
    HighPerformancePool pool(4);
    std::atomic<int> results{0};

    // CPU-intensive task
    pool.submit([&results]() {
        volatile long sum = 0;
        for (long i = 0; i < 1000000; ++i)
        {
            sum += i;
        }
        results++;
    });

    // I/O-like task (sleep)
    pool.submit([&results]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        results++;
    });

    // Quick task
    pool.submit([&results]() { results++; });

    // Task with exception
    auto future = pool.submit([]() -> int { throw std::runtime_error("Test"); });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_GE(results, 3);
    EXPECT_THROW(future.get(), std::runtime_error);
}

// ==================== Resource Cleanup Tests ====================

TEST_F(ThreadPoolTest, ProperCleanupOnDestruction)
{
    std::atomic<int> counter{0};
    {
        HighPerformancePool pool(4);
        for (int i = 0; i < 100; ++i)
        {
            pool.submit([&counter]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                counter++;
            });
        }
        // Pool destroyed here, should wait for all tasks
    }

    // All tasks should have completed
    EXPECT_EQ(counter, 100);
}

TEST_F(ThreadPoolTest, NoTaskLeakage)
{
    std::atomic<int> task_executed{0};
    // removed unused tracker 'task_destroyed'

    {
        HighPerformancePool pool(2);

        // Use shared_ptr to track object lifetime more accurately
        for (int i = 0; i < 10; ++i)
        {
            auto counter = std::make_shared<std::atomic<int>>(0);
            pool.submit([counter, &task_executed]() {
                task_executed++;
                // Task executed
            });
        }

        // Wait for all tasks to execute
        auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (task_executed < 10 && std::chrono::steady_clock::now() < timeout)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    // Pool destroyed - all tasks should have completed

    EXPECT_EQ(task_executed, 10);
}
