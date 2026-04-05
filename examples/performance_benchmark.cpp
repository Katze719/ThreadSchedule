#include <atomic>
#include <benchmark/benchmark.h>
#include <numeric>
#include <random>
#include <threadschedule/threadschedule.hpp>
#include <vector>

using namespace threadschedule;

// =============================================================================
// HighPerformancePool submission throughput (submit with futures)
// =============================================================================

static void BM_HPPool_Throughput(benchmark::State& state)
{
    auto const num_tasks = static_cast<size_t>(state.range(0));

    HighPerformancePool pool(std::thread::hardware_concurrency());
    pool.configure_threads("bench", SchedulingPolicy::OTHER, ThreadPriority::normal());
    pool.distribute_across_cpus();

    std::atomic<size_t> completed{0};

    for (auto _ : state)
    {
        completed = 0;
        std::vector<std::future<void>> futures;
        futures.reserve(num_tasks);

        for (size_t i = 0; i < num_tasks; ++i)
            futures.push_back(pool.submit([&completed]() {
                completed.fetch_add(1, std::memory_order_relaxed);
            }));

        for (auto& f : futures)
            f.wait();

        benchmark::DoNotOptimize(completed.load());
    }

    auto stats = pool.get_statistics();
    state.counters["steal_%"] = 100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1));
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(num_tasks));
}

BENCHMARK(BM_HPPool_Throughput)
    ->Arg(1000)->Arg(10000)->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

// =============================================================================
// HighPerformancePool batch processing
// =============================================================================

static void BM_HPPool_Batch(benchmark::State& state)
{
    auto const batch_size = static_cast<size_t>(state.range(0));

    HighPerformancePool pool(std::thread::hardware_concurrency());
    pool.configure_threads("bench", SchedulingPolicy::OTHER, ThreadPriority::normal());
    pool.distribute_across_cpus();

    std::atomic<size_t> counter{0};
    std::vector<std::function<void()>> tasks;
    tasks.reserve(batch_size);
    for (size_t i = 0; i < batch_size; ++i)
        tasks.emplace_back([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });

    for (auto _ : state)
    {
        auto futures = pool.submit_batch(tasks.begin(), tasks.end());
        for (auto& f : futures)
            f.wait();
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(batch_size));
}

BENCHMARK(BM_HPPool_Batch)
    ->Arg(5000)->Arg(50000)
    ->Unit(benchmark::kMillisecond);

// =============================================================================
// HighPerformancePool variable workload (simulating real tasks)
// =============================================================================

static void BM_HPPool_VariableWorkload(benchmark::State& state)
{
    auto const num_tasks = static_cast<size_t>(state.range(0));

    HighPerformancePool pool(std::thread::hardware_concurrency());
    pool.configure_threads("bench", SchedulingPolicy::OTHER, ThreadPriority::normal());
    pool.distribute_across_cpus();

    std::mt19937 gen(42);
    std::uniform_int_distribution<int> work_dist(10, 200);
    std::vector<int> work_amounts(num_tasks);
    for (auto& w : work_amounts)
        w = work_dist(gen);

    for (auto _ : state)
    {
        std::vector<std::future<void>> futures;
        futures.reserve(num_tasks);

        for (size_t i = 0; i < num_tasks; ++i)
        {
            int const amount = work_amounts[i];
            futures.push_back(pool.submit([amount]() {
                volatile int x = 0;
                for (int j = 0; j < amount; ++j)
                    x += j * j;
            }));
        }

        for (auto& f : futures)
            f.wait();
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(num_tasks));
}

BENCHMARK(BM_HPPool_VariableWorkload)
    ->Arg(1000)->Arg(25000)
    ->Unit(benchmark::kMillisecond);

// =============================================================================
// HighPerformancePool parallel_for_each
// =============================================================================

static void BM_HPPool_ParallelForEach(benchmark::State& state)
{
    auto const data_size = static_cast<size_t>(state.range(0));

    HighPerformancePool pool(std::thread::hardware_concurrency());
    pool.configure_threads("bench", SchedulingPolicy::OTHER, ThreadPriority::normal());
    pool.distribute_across_cpus();

    std::vector<int> data(data_size);
    std::iota(data.begin(), data.end(), 1);

    for (auto _ : state)
    {
        std::atomic<long long> sum{0};
        pool.parallel_for_each(data.begin(), data.end(),
                               [&sum](int v) { sum.fetch_add(v * v, std::memory_order_relaxed); });
        benchmark::DoNotOptimize(sum.load());
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(data_size));
}

BENCHMARK(BM_HPPool_ParallelForEach)
    ->Arg(100000)->Arg(1000000)->Arg(10000000)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
