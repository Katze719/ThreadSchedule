#include <atomic>
#include <chrono>
#include <iomanip> // Required for std::fixed and std::setprecision
#include <iostream>
#include <random>
#include <threadschedule/threadschedule.hpp>
#include <vector>

using namespace threadschedule;

void demonstrate_simple_threadpool()
{
    std::cout << "=== Simple ThreadPool Demo ===" << std::endl;
    std::cout << "Best for: General applications, < 1000 tasks/second" << std::endl;

    ThreadPool pool(4); // Simple thread pool
    pool.configure_threads("SimpleWorker");

    size_t const num_tasks = 100;
    std::atomic<size_t> completed{0};

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::future<void>> futures;
    futures.reserve(num_tasks);

    // Submit individual tasks
    for (size_t i = 0; i < num_tasks; ++i)
    {
        futures.push_back(pool.submit([&completed, i]() {
            // Simulate some work
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            completed.fetch_add(1, std::memory_order_relaxed);
            if (i % 10 == 0)
            {
                std::cout << "Simple task " << i << " completed" << std::endl;
            }
        }));
    }

    // Wait for all tasks
    for (auto& future : futures)
    {
        future.wait();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "Simple pool completed " << completed.load() << " tasks in " << duration.count() << "ms" << std::endl;

    auto stats = pool.get_statistics();
    std::cout << "Pool stats: " << stats.completed_tasks << " total completed" << std::endl;
}

void demonstrate_high_performance_pool()
{
    std::cout << "\n=== HighPerformancePool Demo ===" << std::endl;
    std::cout << "Best for: High-frequency workloads, 10k+ tasks/second" << std::endl;

    HighPerformancePool pool(std::thread::hardware_concurrency());

    // Configure for high performance
    pool.configure_threads("HighPerf", SchedulingPolicy::OTHER, ThreadPriority::normal());
    pool.distribute_across_cpus();

    size_t const num_tasks = 10000;
    std::atomic<size_t> completed_counter{0};

    auto start_time = std::chrono::high_resolution_clock::now();

    // Submit many lightweight tasks
    std::vector<std::future<void>> futures;
    futures.reserve(num_tasks);

    for (size_t i = 0; i < num_tasks; ++i)
    {
        futures.push_back(pool.submit([&completed_counter]() {
            // Lightweight CPU work
            volatile int x = 0;
            for (int j = 0; j < 100; ++j)
            {
                x += j * j;
            }
            completed_counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    // Wait for completion
    for (auto& future : futures)
    {
        future.wait();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    double tasks_per_second = static_cast<double>(num_tasks * 1000) / duration.count();

    std::cout << "High-performance pool completed " << completed_counter.load() << " tasks in " << duration.count()
              << "ms" << std::endl;
    std::cout << "Throughput: " << static_cast<size_t>(tasks_per_second) << " tasks/second" << std::endl;

    auto stats = pool.get_statistics();
    std::cout << "Performance stats:" << std::endl;
    std::cout << "  Completed: " << stats.completed_tasks << std::endl;
    std::cout << "  Work stolen: " << stats.stolen_tasks << std::endl;
    std::cout << "  Avg task time: " << stats.avg_task_time.count() << "μs" << std::endl;
    std::cout << "  Stealing ratio: " << std::fixed << std::setprecision(1)
              << (100.0 * stats.stolen_tasks / stats.completed_tasks) << "%" << std::endl;
}

void demonstrate_batch_processing()
{
    std::cout << "\n=== Batch Processing Demo (HighPerformancePool) ===" << std::endl;

    HighPerformancePool pool(std::thread::hardware_concurrency());

    size_t const batch_size = 5000;
    std::vector<std::function<void()>> tasks;
    std::atomic<size_t> counter{0};

    // Prepare batch
    tasks.reserve(batch_size);
    for (size_t i = 0; i < batch_size; ++i)
    {
        tasks.emplace_back([&counter]() {
            // Simulate work
            volatile int x = 0;
            for (int j = 0; j < 50; ++j)
            {
                x += j;
            }
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Submit entire batch at once - more efficient than individual submits
    auto futures = pool.submit_batch(tasks.begin(), tasks.end());

    for (auto& future : futures)
    {
        future.wait();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    double tasks_per_second = (static_cast<double>(batch_size) * 1000000.0) / duration.count();

    std::cout << "Batch processing: " << counter.load() << " tasks in " << duration.count() << "μs" << std::endl;
    std::cout << "Batch throughput: " << std::fixed << std::setprecision(0) << tasks_per_second << " tasks/second"
              << std::endl;
}

void demonstrate_global_pools()
{
    std::cout << "\n=== Global Thread Pools Demo ===" << std::endl;

    // Simple global pool
    std::cout << "Using GlobalThreadPool (simple):" << std::endl;
    auto simple_future = GlobalThreadPool::submit([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 42;
    });
    std::cout << "Simple global result: " << simple_future.get() << std::endl;

    // High-performance global pool
    std::cout << "Using GlobalHighPerformancePool:" << std::endl;
    auto hp_future = GlobalHighPerformancePool::submit([]() {
        volatile int sum = 0;
        for (int i = 0; i < 1000; ++i)
        {
            sum += i;
        }
        return sum;
    });
    std::cout << "High-performance global result: " << hp_future.get() << std::endl;

    // Parallel algorithm (uses simple pool by default)
    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::cout << "Original data: ";
    for (int x : data)
        std::cout << x << " ";
    std::cout << std::endl;

    parallel_for_each(data, [](int& x) {
        x *= x; // Square each element
    });

    std::cout << "Squared data: ";
    for (int x : data)
        std::cout << x << " ";
    std::cout << std::endl;
}

int main()
{
    std::cout << "ThreadSchedule Thread Pool Comparison Demo" << std::endl;
    std::cout << "Hardware threads: " << std::thread::hardware_concurrency() << std::endl;
    std::cout << "==========================================" << std::endl;

    try
    {
        demonstrate_simple_threadpool();
        demonstrate_high_performance_pool();
        demonstrate_batch_processing();
        demonstrate_global_pools();

        std::cout << "\n=== Pool Selection Guide ===" << std::endl;
        std::cout << "ThreadPool (Simple):" << std::endl;
        std::cout << "  ✓ Use for general applications" << std::endl;
        std::cout << "  ✓ Task rate < 1000/second" << std::endl;
        std::cout << "  ✓ Lower memory usage" << std::endl;
        std::cout << "  ✓ Easier to debug" << std::endl;

        std::cout << "\nHighPerformancePool (Work-Stealing):" << std::endl;
        std::cout << "  ✓ Use for high-frequency workloads" << std::endl;
        std::cout << "  ✓ Task rate > 10k/second" << std::endl;
        std::cout << "  ✓ Batch processing support" << std::endl;
        std::cout << "  ✓ Advanced performance monitoring" << std::endl;
        std::cout << "  ✓ Optimal for short CPU-bound tasks" << std::endl;
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
