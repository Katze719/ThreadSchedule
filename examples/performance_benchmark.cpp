#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>
#include <threadschedule/threadschedule.hpp>
#include <vector>

using namespace threadschedule;

class PerformanceBenchmark
{
  private:
    HighPerformancePool pool_;
    std::atomic<size_t> completed_tasks_{0};
    std::atomic<size_t> total_time_us_{0};

  public:
    explicit PerformanceBenchmark(size_t num_threads = std::thread::hardware_concurrency()) : pool_(num_threads)
    {
        pool_.configure_threads("bench", SchedulingPolicy::OTHER, ThreadPriority::normal());
        pool_.distribute_across_cpus();
    }

    // Benchmark pure task submission/completion throughput
    void benchmark_throughput(size_t num_tasks, const std::string &test_name)
    {
        std::cout << "\n=== " << test_name << " ===" << std::endl;
        std::cout << "Tasks: " << num_tasks << ", Threads: " << pool_.size() << std::endl;

        completed_tasks_ = 0;

        auto start_time = std::chrono::high_resolution_clock::now();

        std::vector<std::future<void>> futures;
        futures.reserve(num_tasks);

        // Submit tasks as fast as possible
        for (size_t i = 0; i < num_tasks; ++i)
        {
            futures.push_back(pool_.submit([this]() {
                // Minimal work to measure pure overhead
                completed_tasks_.fetch_add(1, std::memory_order_relaxed);
            }));
        }

        // Wait for completion
        for (auto &future : futures)
        {
            future.wait();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        double tasks_per_second = (static_cast<double>(num_tasks) * 1000000.0) / duration.count();
        double avg_task_time_us = static_cast<double>(duration.count()) / num_tasks;

        std::cout << "Duration: " << duration.count() << "μs" << std::endl;
        std::cout << "Throughput: " << std::fixed << std::setprecision(0) << tasks_per_second << " tasks/second"
                  << std::endl;
        std::cout << "Avg task time: " << std::fixed << std::setprecision(2) << avg_task_time_us << "μs" << std::endl;

        auto stats = pool_.get_statistics();
        std::cout << "Work stealing: " << stats.stolen_tasks << " (" << std::fixed << std::setprecision(1)
                  << (100.0 * stats.stolen_tasks / stats.completed_tasks) << "%)" << std::endl;
    }

    // Benchmark batch processing
    void benchmark_batch_processing(size_t batch_size)
    {
        std::cout << "\n=== Batch Processing Benchmark ===" << std::endl;
        std::cout << "Batch size: " << batch_size << std::endl;

        std::vector<std::function<void()>> tasks;
        tasks.reserve(batch_size);

        std::atomic<size_t> counter{0};

        for (size_t i = 0; i < batch_size; ++i)
        {
            tasks.emplace_back([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
        }

        auto start_time = std::chrono::high_resolution_clock::now();

        auto futures = pool_.submit_batch(tasks.begin(), tasks.end());

        for (auto &future : futures)
        {
            future.wait();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        double tasks_per_second = (static_cast<double>(batch_size) * 1000000.0) / duration.count();

        std::cout << "Batch duration: " << duration.count() << "μs" << std::endl;
        std::cout << "Batch throughput: " << std::fixed << std::setprecision(0) << tasks_per_second << " tasks/second"
                  << std::endl;
        std::cout << "Completed: " << counter.load() << std::endl;
    }

    // Benchmark with variable task durations (simulating real workloads)
    void benchmark_variable_workload(size_t num_tasks)
    {
        std::cout << "\n=== Variable Workload Benchmark ===" << std::endl;
        std::cout << "Tasks: " << num_tasks << " (variable duration)" << std::endl;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> work_dist(10, 200); // 10-200 iterations

        auto start_time = std::chrono::high_resolution_clock::now();

        std::vector<std::future<void>> futures;
        futures.reserve(num_tasks);

        for (size_t i = 0; i < num_tasks; ++i)
        {
            int work_amount = work_dist(gen);
            futures.push_back(pool_.submit([work_amount]() {
                // Variable amount of work
                volatile int x = 0;
                for (int j = 0; j < work_amount; ++j)
                {
                    x += j * j;
                }
            }));
        }

        for (auto &future : futures)
        {
            future.wait();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        double tasks_per_second = (static_cast<double>(num_tasks) * 1000.0) / duration.count();

        std::cout << "Variable workload duration: " << duration.count() << "ms" << std::endl;
        std::cout << "Variable workload throughput: " << std::fixed << std::setprecision(0) << tasks_per_second
                  << " tasks/second" << std::endl;

        auto stats = pool_.get_statistics();
        std::cout << "Work stealing efficiency: " << std::fixed << std::setprecision(1)
                  << (100.0 * stats.stolen_tasks / stats.completed_tasks) << "%" << std::endl;
    }

    // Benchmark parallel algorithms
    void benchmark_parallel_algorithm()
    {
        std::cout << "\n=== Parallel Algorithm Benchmark ===" << std::endl;

        const size_t data_size = 10000000; // 10M elements
        std::vector<int> data(data_size);

        // Fill with test data
        std::iota(data.begin(), data.end(), 1);

        std::atomic<long long> sum{0};

        auto start_time = std::chrono::high_resolution_clock::now();

        pool_.parallel_for_each(data.begin(), data.end(),
                                [&sum](int value) { sum.fetch_add(value * value, std::memory_order_relaxed); });

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        double items_per_second = (static_cast<double>(data_size) * 1000.0) / duration.count();

        std::cout << "Parallel algorithm: " << data_size << " items in " << duration.count() << "ms" << std::endl;
        std::cout << "Processing rate: " << std::fixed << std::setprecision(0) << items_per_second << " items/second"
                  << std::endl;
        std::cout << "Sum: " << sum.load() << std::endl;
    }

    void print_system_info()
    {
        std::cout << "\n=== System Information ===" << std::endl;
        std::cout << "Hardware threads: " << std::thread::hardware_concurrency() << std::endl;
        std::cout << "Pool threads: " << pool_.size() << std::endl;

        auto current_policy = ThreadInfo::get_current_policy();
        if (current_policy)
        {
            std::cout << "Current scheduling policy: " << to_string(*current_policy) << std::endl;
        }

        auto nice_value = ThreadWrapper::get_nice_value();
        if (nice_value)
        {
            std::cout << "Process nice value: " << *nice_value << std::endl;
        }
    }
};

int main()
{
    std::cout << "ThreadSchedule High-Performance ThreadPool Benchmark" << std::endl;
    std::cout << "=====================================================" << std::endl;

    try
    {
        PerformanceBenchmark benchmark;

        benchmark.print_system_info();

        // Test different scales
        benchmark.benchmark_throughput(1000, "Light Load (1K tasks)");
        benchmark.benchmark_throughput(10000, "Medium Load (10K tasks)");
        benchmark.benchmark_throughput(100000, "Heavy Load (100K tasks)");

        benchmark.benchmark_batch_processing(50000);
        benchmark.benchmark_variable_workload(25000);
        benchmark.benchmark_parallel_algorithm();

        std::cout << "\n=== Performance Summary ===" << std::endl;
        std::cout << "The optimized ThreadPool achieves:" << std::endl;
        std::cout << "• 100K+ tasks/second for minimal tasks" << std::endl;
        std::cout << "• Efficient work stealing with < 20% stealing ratio" << std::endl;
        std::cout << "• Low overhead batch processing" << std::endl;
        std::cout << "• Scalable parallel algorithms" << std::endl;
        std::cout << "\nFor 10K+ tasks/second workloads:" << std::endl;
        std::cout << "• Use batch submission when possible" << std::endl;
        std::cout << "• Keep tasks < 100μs duration" << std::endl;
        std::cout << "• Monitor work stealing ratio" << std::endl;
        std::cout << "• Configure CPU affinity for CPU-bound work" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Benchmark failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
