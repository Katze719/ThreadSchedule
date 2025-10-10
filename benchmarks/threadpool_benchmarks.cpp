#include <algorithm>
#include <atomic>
#include <benchmark/benchmark.h>
#include <chrono>
#include <random>
#include <threadschedule/threadschedule.hpp>
#include <vector>

using namespace threadschedule;

// Benchmark configuration
// constexpr size_t MIN_THREADS = 1;
// constexpr size_t MAX_THREADS = 16; // Currently unused, kept for future reference

// Task workloads for different scenarios
struct BenchmarkWorkloads
{
    // Minimal task (testing pure overhead)
    static void minimal_task()
    {
        volatile int x = 42;
        (void)x;
    }

    // Light CPU work (simulating typical web service task)
    static void light_cpu_task()
    {
        volatile int sum = 0;
        for (int i = 0; i < 100; ++i)
        {
            sum += i * i;
        }
    }

    // Medium CPU work
    static void medium_cpu_task()
    {
        volatile int sum = 0;
        for (int i = 0; i < 1000; ++i)
        {
            sum += i * i;
        }
    }

    // Heavy CPU work
    static void heavy_cpu_task()
    {
        volatile int sum = 0;
        for (int i = 0; i < 10000; ++i)
        {
            sum += i * i;
        }
    }
};

// =============================================================================
// Basic ThreadPool Benchmarks
// =============================================================================

static void BM_ThreadPool_MinimalTasks(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const num_tasks = state.range(1);

    ThreadPool pool(num_threads);
    pool.configure_threads("bench");

    for (auto _ : state)
    {
        std::vector<std::future<void>> futures;
        futures.reserve(num_tasks);

        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < num_tasks; ++i)
        {
            futures.push_back(pool.submit(BenchmarkWorkloads::minimal_task));
        }

        for (auto& future : futures)
        {
            future.wait();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1e9);
    }

    state.SetItemsProcessed(state.iterations() * num_tasks);
    state.SetLabel("threads=" + std::to_string(num_threads) + " tasks=" + std::to_string(num_tasks));
}

static void BM_ThreadPool_LightTasks(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const num_tasks = state.range(1);

    ThreadPool pool(num_threads);
    pool.configure_threads("bench");

    for (auto _ : state)
    {
        std::vector<std::future<void>> futures;
        futures.reserve(num_tasks);

        for (size_t i = 0; i < num_tasks; ++i)
        {
            futures.push_back(pool.submit(BenchmarkWorkloads::light_cpu_task));
        }

        for (auto& future : futures)
        {
            future.wait();
        }
    }

    state.SetItemsProcessed(state.iterations() * num_tasks);
    state.SetLabel("threads=" + std::to_string(num_threads));
}

// =============================================================================
// FastThreadPool Benchmarks
// =============================================================================

static void BM_FastThreadPool_MinimalTasks(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const num_tasks = state.range(1);

    FastThreadPool pool(num_threads);
    pool.configure_threads("bench");

    for (auto _ : state)
    {
        std::vector<std::future<void>> futures;
        futures.reserve(num_tasks);

        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < num_tasks; ++i)
        {
            futures.push_back(pool.submit(BenchmarkWorkloads::minimal_task));
        }

        for (auto& future : futures)
        {
            future.wait();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1e9);
    }

    state.SetItemsProcessed(state.iterations() * num_tasks);
    state.SetLabel("threads=" + std::to_string(num_threads) + " tasks=" + std::to_string(num_tasks));
}

static void BM_FastThreadPool_BatchProcessing(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const batch_size = state.range(1);

    FastThreadPool pool(num_threads);
    pool.configure_threads("bench");

    std::vector<std::function<void()>> tasks;
    tasks.reserve(batch_size);
    for (size_t i = 0; i < batch_size; ++i)
    {
        tasks.emplace_back(BenchmarkWorkloads::light_cpu_task);
    }

    for (auto _ : state)
    {
        auto futures = pool.submit_batch(tasks.begin(), tasks.end());
        for (auto& future : futures)
        {
            future.wait();
        }
    }

    state.SetItemsProcessed(state.iterations() * batch_size);
    state.SetLabel("threads=" + std::to_string(num_threads) + " batch=" + std::to_string(batch_size));
}

// =============================================================================
// HighPerformancePool Benchmarks (Work-Stealing)
// =============================================================================

static void BM_HighPerformancePool_MinimalTasks(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const num_tasks = state.range(1);

    HighPerformancePool pool(num_threads);
    pool.configure_threads("bench", SchedulingPolicy::OTHER, ThreadPriority::normal());
    pool.distribute_across_cpus();

    for (auto _ : state)
    {
        std::vector<std::future<void>> futures;
        futures.reserve(num_tasks);

        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < num_tasks; ++i)
        {
            futures.push_back(pool.submit(BenchmarkWorkloads::minimal_task));
        }

        for (auto& future : futures)
        {
            future.wait();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1e9);
    }

    auto stats = pool.get_statistics();
    state.counters["work_steal_ratio"] = 100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1));
    state.SetItemsProcessed(state.iterations() * num_tasks);
    state.SetLabel("threads=" + std::to_string(num_threads) + " tasks=" + std::to_string(num_tasks));
}

static void BM_HighPerformancePool_BatchProcessing(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const batch_size = state.range(1);

    HighPerformancePool pool(num_threads);
    pool.configure_threads("bench", SchedulingPolicy::OTHER, ThreadPriority::normal());
    pool.distribute_across_cpus();

    std::vector<std::function<void()>> tasks;
    tasks.reserve(batch_size);
    for (size_t i = 0; i < batch_size; ++i)
    {
        tasks.emplace_back(BenchmarkWorkloads::light_cpu_task);
    }

    for (auto _ : state)
    {
        auto futures = pool.submit_batch(tasks.begin(), tasks.end());
        for (auto& future : futures)
        {
            future.wait();
        }
    }

    auto stats = pool.get_statistics();
    state.counters["work_steal_ratio"] = 100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1));
    state.counters["tasks_per_second"] = stats.tasks_per_second;
    state.SetItemsProcessed(state.iterations() * batch_size);
    state.SetLabel("threads=" + std::to_string(num_threads) + " batch=" + std::to_string(batch_size));
}

static void BM_HighPerformancePool_ParallelForEach(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const data_size = state.range(1);

    HighPerformancePool pool(num_threads);
    pool.configure_threads("bench", SchedulingPolicy::OTHER, ThreadPriority::normal());
    pool.distribute_across_cpus();

    std::vector<int> data(data_size);
    std::iota(data.begin(), data.end(), 1);

    for (auto _ : state)
    {
        std::atomic<long long> sum{0};

        pool.parallel_for_each(data.begin(), data.end(),
                               [&sum](int value) { sum.fetch_add(value * value, std::memory_order_relaxed); });

        benchmark::DoNotOptimize(sum.load());
    }

    state.SetItemsProcessed(state.iterations() * data_size);
    state.SetLabel("threads=" + std::to_string(num_threads) + " items=" + std::to_string(data_size));
}

// =============================================================================
// Comparison Benchmarks (All Pools)
// =============================================================================

static void BM_ComparePoolTypes_LightWorkload(benchmark::State& state)
{
    size_t const num_threads = 4; // Fixed for fair comparison
    size_t const num_tasks = state.range(0);
    int const pool_type = state.range(1); // 0=ThreadPool, 1=FastThreadPool, 2=HighPerformancePool

    for (auto _ : state)
    {
        state.PauseTiming();

        std::vector<std::future<void>> futures;
        futures.reserve(num_tasks);

        state.ResumeTiming();

        if (pool_type == 0)
        {
            ThreadPool pool(num_threads);
            pool.configure_threads("bench");

            for (size_t i = 0; i < num_tasks; ++i)
            {
                futures.push_back(pool.submit(BenchmarkWorkloads::light_cpu_task));
            }

            for (auto& future : futures)
            {
                future.wait();
            }
        }
        else if (pool_type == 1)
        {
            FastThreadPool pool(num_threads);
            pool.configure_threads("bench");

            for (size_t i = 0; i < num_tasks; ++i)
            {
                futures.push_back(pool.submit(BenchmarkWorkloads::light_cpu_task));
            }

            for (auto& future : futures)
            {
                future.wait();
            }
        }
        else if (pool_type == 2)
        {
            HighPerformancePool pool(num_threads);
            pool.configure_threads("bench");
            pool.distribute_across_cpus();

            for (size_t i = 0; i < num_tasks; ++i)
            {
                futures.push_back(pool.submit(BenchmarkWorkloads::light_cpu_task));
            }

            for (auto& future : futures)
            {
                future.wait();
            }
        }
    }

    std::vector<std::string> pool_names = {"ThreadPool", "FastThreadPool", "HighPerformancePool"};
    state.SetItemsProcessed(state.iterations() * num_tasks);
    state.SetLabel(pool_names[pool_type] + " tasks=" + std::to_string(num_tasks));
}

// =============================================================================
// Registration with various parameter combinations
// =============================================================================

// Basic throughput tests for each pool type
BENCHMARK(BM_ThreadPool_MinimalTasks)
    ->Args({1, 100})
    ->Args({2, 100})
    ->Args({4, 100})
    ->Args({8, 100})
    ->Args({1, 1000})
    ->Args({2, 1000})
    ->Args({4, 1000})
    ->Args({8, 1000})
    ->Args({1, 10000})
    ->Args({4, 10000})
    ->Args({8, 10000})
    ->UseManualTime()
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_ThreadPool_LightTasks)
    ->Args({1, 100})
    ->Args({2, 100})
    ->Args({4, 100})
    ->Args({8, 100})
    ->Args({1, 1000})
    ->Args({4, 1000})
    ->Args({8, 1000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_FastThreadPool_MinimalTasks)
    ->Args({1, 100})
    ->Args({2, 100})
    ->Args({4, 100})
    ->Args({8, 100})
    ->Args({1, 1000})
    ->Args({2, 1000})
    ->Args({4, 1000})
    ->Args({8, 1000})
    ->Args({1, 10000})
    ->Args({4, 10000})
    ->Args({8, 10000})
    ->UseManualTime()
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_FastThreadPool_BatchProcessing)
    ->Args({1, 1000})
    ->Args({2, 1000})
    ->Args({4, 1000})
    ->Args({8, 1000})
    ->Args({4, 5000})
    ->Args({8, 5000})
    ->Args({4, 10000})
    ->Args({8, 10000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_HighPerformancePool_MinimalTasks)
    ->Args({1, 100})
    ->Args({2, 100})
    ->Args({4, 100})
    ->Args({8, 100})
    ->Args({1, 1000})
    ->Args({2, 1000})
    ->Args({4, 1000})
    ->Args({8, 1000})
    ->Args({1, 10000})
    ->Args({4, 10000})
    ->Args({8, 10000})
    ->Args({16, 10000})
    ->Args({4, 100000})
    ->Args({8, 100000})
    ->Args({16, 100000})
    ->UseManualTime()
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_HighPerformancePool_BatchProcessing)
    ->Args({1, 1000})
    ->Args({2, 1000})
    ->Args({4, 1000})
    ->Args({8, 1000})
    ->Args({4, 5000})
    ->Args({8, 5000})
    ->Args({16, 5000})
    ->Args({4, 10000})
    ->Args({8, 10000})
    ->Args({16, 10000})
    ->Args({8, 50000})
    ->Args({16, 50000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_HighPerformancePool_ParallelForEach)
    ->Args({1, 10000})
    ->Args({2, 10000})
    ->Args({4, 10000})
    ->Args({8, 10000})
    ->Args({4, 100000})
    ->Args({8, 100000})
    ->Args({16, 100000})
    ->Args({8, 1000000})
    ->Args({16, 1000000})
    ->Unit(benchmark::kMillisecond);

// Pool comparison benchmarks
BENCHMARK(BM_ComparePoolTypes_LightWorkload)
    ->Args({100, 0})
    ->Args({100, 1})
    ->Args({100, 2}) // 100 tasks, different pools
    ->Args({1000, 0})
    ->Args({1000, 1})
    ->Args({1000, 2}) // 1K tasks, different pools
    ->Args({10000, 0})
    ->Args({10000, 1})
    ->Args({10000, 2}) // 10K tasks, different pools
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
