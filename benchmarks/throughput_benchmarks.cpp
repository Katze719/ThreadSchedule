#include <atomic>
#include <benchmark/benchmark.h>
#include <chrono>
#include <memory>
#include <random>
#include <threadschedule/threadschedule.hpp>
#include <vector>

using namespace threadschedule;

// =============================================================================
// High-Throughput Benchmarks (10k+ tasks/second scenarios)
// =============================================================================

static void BM_HighThroughput_HighPerformancePool(benchmark::State &state)
{
    const size_t num_threads = state.range(0);
    const size_t tasks_per_iteration = state.range(1);

    HighPerformancePool pool(num_threads);
    pool.configure_threads("htp_bench", SchedulingPolicy::OTHER, ThreadPriority::normal());
    pool.distribute_across_cpus();

    for (auto _ : state)
    {
        std::vector<std::future<void>> futures;
        futures.reserve(tasks_per_iteration);

        // Submit tasks as fast as possible
        for (size_t i = 0; i < tasks_per_iteration; ++i)
        {
            futures.push_back(pool.submit([]() {
                // Minimal work to test pure overhead
                std::this_thread::yield();
            }));
        }

        // Wait for all tasks to complete
        for (auto &future : futures)
        {
            future.wait();
        }
    }

    auto stats = pool.get_statistics();
    state.counters["tasks_per_second"] = benchmark::Counter(stats.tasks_per_second);
    state.counters["work_steal_ratio"] =
        benchmark::Counter(100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1)));
    state.SetItemsProcessed(state.iterations() * tasks_per_iteration);
}

static void BM_HighThroughput_FastThreadPool(benchmark::State &state)
{
    const size_t num_threads = state.range(0);
    const size_t tasks_per_iteration = state.range(1);

    FastThreadPool pool(num_threads);
    pool.configure_threads("ftp_bench");

    for (auto _ : state)
    {
        std::vector<std::future<void>> futures;
        futures.reserve(tasks_per_iteration);

        for (size_t i = 0; i < tasks_per_iteration; ++i)
        {
            futures.push_back(pool.submit([]() { std::this_thread::yield(); }));
        }

        for (auto &future : futures)
        {
            future.wait();
        }
    }

    auto stats = pool.get_statistics();
    state.counters["tasks_per_second"] = benchmark::Counter(stats.tasks_per_second);
    state.SetItemsProcessed(state.iterations() * tasks_per_iteration);
}

// =============================================================================
// Scalability Benchmarks (How performance changes with thread count)
// =============================================================================

static void BM_Scalability_WorkStealing(benchmark::State &state)
{
    const size_t num_threads = state.range(0);
    const size_t num_tasks = 50000; // Fixed task count

    HighPerformancePool pool(num_threads);
    pool.configure_threads("scale_bench", SchedulingPolicy::OTHER, ThreadPriority::normal());
    pool.distribute_across_cpus();

    // Create variable workload to encourage work stealing
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> work_dist(50, 500);

    for (auto _ : state)
    {
        std::vector<std::future<void>> futures;
        futures.reserve(num_tasks);

        for (size_t i = 0; i < num_tasks; ++i)
        {
            int work_amount = work_dist(gen);
            futures.push_back(pool.submit([work_amount]() {
                volatile int sum = 0;
                for (int j = 0; j < work_amount; ++j)
                {
                    sum += j;
                }
            }));
        }

        for (auto &future : futures)
        {
            future.wait();
        }
    }

    auto stats = pool.get_statistics();
    state.counters["work_steal_ratio"] =
        benchmark::Counter(100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1)));
    state.counters["efficiency"] =
        benchmark::Counter(static_cast<double>(num_tasks) / num_threads / state.iterations());
    state.SetItemsProcessed(state.iterations() * num_tasks);
}

// =============================================================================
// Contention Benchmarks (High contention scenarios)
// =============================================================================

static void BM_Contention_SubmissionStorm(benchmark::State &state)
{
    const size_t num_threads = state.range(0);
    const size_t num_submitters = state.range(1); // Number of threads submitting tasks

    HighPerformancePool pool(num_threads);
    pool.configure_threads("contention_bench");

    for (auto _ : state)
    {
        std::atomic<size_t> submitted_tasks{0};
        std::atomic<size_t> completed_tasks{0};
        std::vector<std::thread> submitters;

        const size_t tasks_per_submitter = 1000;

        // Start submitter threads
        for (size_t i = 0; i < num_submitters; ++i)
        {
            submitters.emplace_back([&pool, &submitted_tasks, &completed_tasks, tasks_per_submitter]() {
                std::vector<std::future<void>> futures;
                futures.reserve(tasks_per_submitter);

                for (size_t j = 0; j < tasks_per_submitter; ++j)
                {
                    futures.push_back(
                        pool.submit([&completed_tasks]() { completed_tasks.fetch_add(1, std::memory_order_relaxed); }));
                    submitted_tasks.fetch_add(1, std::memory_order_relaxed);
                }

                for (auto &future : futures)
                {
                    future.wait();
                }
            });
        }

        // Wait for all submitters to finish
        for (auto &submitter : submitters)
        {
            submitter.join();
        }
    }

    state.SetItemsProcessed(state.iterations() * num_submitters * 1000);
}

// =============================================================================
// Memory Access Pattern Benchmarks
// =============================================================================

static void BM_MemoryAccess_Sequential(benchmark::State &state)
{
    const size_t num_threads = state.range(0);
    const size_t data_size = 1000000; // 1M elements

    HighPerformancePool pool(num_threads);
    pool.configure_threads("mem_bench");

    std::vector<int> data(data_size);
    std::iota(data.begin(), data.end(), 1);

    for (auto _ : state)
    {
        std::atomic<long long> sum{0};

        pool.parallel_for_each(data.begin(), data.end(),
                               [&sum](int value) { sum.fetch_add(value, std::memory_order_relaxed); });

        benchmark::DoNotOptimize(sum.load());
    }

    state.SetItemsProcessed(state.iterations() * data_size);
    state.counters["elements_per_second"] = benchmark::Counter(data_size, benchmark::Counter::kIsRate);
}

static void BM_MemoryAccess_Random(benchmark::State &state)
{
    const size_t num_threads = state.range(0);
    const size_t data_size = 1000000;

    HighPerformancePool pool(num_threads);
    pool.configure_threads("mem_rand_bench");

    std::vector<int> data(data_size);
    std::vector<size_t> indices(data_size);
    std::iota(data.begin(), data.end(), 1);
    std::iota(indices.begin(), indices.end(), 0);

    // Shuffle indices for random access pattern
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(indices.begin(), indices.end(), gen);

    for (auto _ : state)
    {
        std::atomic<long long> sum{0};

        pool.parallel_for_each(indices.begin(), indices.end(),
                               [&sum, &data](size_t idx) { sum.fetch_add(data[idx], std::memory_order_relaxed); });

        benchmark::DoNotOptimize(sum.load());
    }

    state.SetItemsProcessed(state.iterations() * data_size);
    state.counters["elements_per_second"] = benchmark::Counter(data_size, benchmark::Counter::kIsRate);
}

// =============================================================================
// Registration
// =============================================================================

// High throughput tests
BENCHMARK(BM_HighThroughput_HighPerformancePool)
    ->Args({1, 10000})
    ->Args({2, 10000})
    ->Args({4, 10000})
    ->Args({8, 10000})
    ->Args({4, 50000})
    ->Args({8, 50000})
    ->Args({16, 50000})
    ->Args({8, 100000})
    ->Args({16, 100000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_HighThroughput_FastThreadPool)
    ->Args({1, 10000})
    ->Args({2, 10000})
    ->Args({4, 10000})
    ->Args({8, 10000})
    ->Args({4, 50000})
    ->Args({8, 50000})
    ->Unit(benchmark::kMillisecond);

// Scalability tests
BENCHMARK(BM_Scalability_WorkStealing)
    ->DenseRange(1, 16,
                 1) // 1 to 16 threads
    ->Unit(benchmark::kMillisecond);

// Contention tests
BENCHMARK(BM_Contention_SubmissionStorm)
    ->Args({4, 1})
    ->Args({4, 2})
    ->Args({4, 4})
    ->Args({4, 8}) // 4 worker threads, varying submitters
    ->Args({8, 1})
    ->Args({8, 2})
    ->Args({8, 4})
    ->Args({8, 8}) // 8 worker threads, varying submitters
    ->Unit(benchmark::kMillisecond);

// Memory access patterns
BENCHMARK(BM_MemoryAccess_Sequential)
    ->Args({1})
    ->Args({2})
    ->Args({4})
    ->Args({8})
    ->Args({16})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_MemoryAccess_Random)
    ->Args({1})
    ->Args({2})
    ->Args({4})
    ->Args({8})
    ->Args({16})
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
