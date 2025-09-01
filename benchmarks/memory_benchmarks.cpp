#include <atomic>
#include <benchmark/benchmark.h>
#include <memory>
#include <numeric>
#include <random>
#include <threadschedule/threadschedule.hpp>
#include <vector>

using namespace threadschedule;

// =============================================================================
// Cache Line Performance Benchmarks
// =============================================================================

struct CacheLineBenchmark
{
    static constexpr size_t CACHE_LINE_SIZE     = 64;
    static constexpr size_t INTS_PER_CACHE_LINE = CACHE_LINE_SIZE / sizeof(int);

    // Test cache-friendly access patterns
    static void cache_friendly_task(
        std::vector<int> &data,
        size_t            start_idx,
        size_t            count
    )
    {
        for (size_t i = 0; i < count; ++i)
        {
            data[start_idx + i] += 1;
        }
    }

    // Test cache-unfriendly access patterns (strided access)
    static void cache_unfriendly_task(
        std::vector<int> &data,
        size_t            start_idx,
        size_t            stride,
        size_t            count
    )
    {
        for (size_t i = 0; i < count; ++i)
        {
            data[start_idx + i * stride] += 1;
        }
    }
};

static void BM_CacheFriendly_HighPerformancePool(benchmark::State &state)
{
    const size_t num_threads = state.range(0);
    const size_t data_size   = 1000000; // 1M integers

    HighPerformancePool pool(num_threads);
    pool.configure_threads("cache_bench");
    pool.distribute_across_cpus();

    std::vector<int> data(data_size, 1);
    const size_t     chunk_size = data_size / (num_threads * 4); // Optimize for cache

    for (auto _ : state)
    {
        std::vector<std::future<void>> futures;

        for (size_t i = 0; i < data_size; i += chunk_size)
        {
            const size_t end_idx = std::min(i + chunk_size, data_size);
            futures.push_back(pool.submit([&data, i, end_idx]() {
                CacheLineBenchmark::cache_friendly_task(data, i, end_idx - i);
            }));
        }

        for (auto &future : futures)
        {
            future.wait();
        }

        benchmark::DoNotOptimize(data.data());
    }

    state.SetItemsProcessed(state.iterations() * data_size);
    state.counters["cache_efficiency"] = benchmark::Counter(1.0); // Cache-friendly = 1.0
}

static void BM_CacheUnfriendly_HighPerformancePool(benchmark::State &state)
{
    const size_t num_threads = state.range(0);
    const size_t data_size   = 1000000;

    HighPerformancePool pool(num_threads);
    pool.configure_threads("cache_unfriendly_bench");

    std::vector<int> data(data_size, 1);
    const size_t     stride              = 64; // Jump by cache lines
    const size_t     elements_per_thread = data_size / (num_threads * stride);

    for (auto _ : state)
    {
        std::vector<std::future<void>> futures;

        for (size_t t = 0; t < num_threads; ++t)
        {
            futures.push_back(pool.submit([&data, t, stride, elements_per_thread]() {
                CacheLineBenchmark::cache_unfriendly_task(data, t, stride, elements_per_thread);
            }));
        }

        for (auto &future : futures)
        {
            future.wait();
        }

        benchmark::DoNotOptimize(data.data());
    }

    state.SetItemsProcessed(state.iterations() * data_size);
    state.counters["cache_efficiency"] = benchmark::Counter(0.1); // Cache-unfriendly = 0.1
}

// =============================================================================
// Memory Allocation Benchmarks
// =============================================================================

static void BM_MemoryAllocation_TaskCreation(benchmark::State &state)
{
    const size_t num_threads     = state.range(0);
    const size_t num_allocations = state.range(1);

    HighPerformancePool pool(num_threads);
    pool.configure_threads("alloc_bench");

    for (auto _ : state)
    {
        std::vector<std::future<std::unique_ptr<std::vector<int>>>> futures;
        futures.reserve(num_allocations);

        for (size_t i = 0; i < num_allocations; ++i)
        {
            futures.push_back(pool.submit([]() -> std::unique_ptr<std::vector<int>> {
                auto vec = std::make_unique<std::vector<int>>(1000);
                std::iota(vec->begin(), vec->end(), 1);
                return vec;
            }));
        }

        // Collect results to ensure allocations aren't optimized away
        size_t total_sum = 0;
        for (auto &future : futures)
        {
            auto result = future.get();
            total_sum += result->size();
        }

        benchmark::DoNotOptimize(total_sum);
    }

    state.SetItemsProcessed(state.iterations() * num_allocations);
}

// =============================================================================
// NUMA Awareness Benchmarks (for multi-socket systems)
// =============================================================================

static void BM_NUMA_LocalMemory(benchmark::State &state)
{
    const size_t num_threads = state.range(0);
    const size_t data_size   = 10000000; // 10M integers

    HighPerformancePool pool(num_threads);
    pool.configure_threads("numa_bench");
    pool.distribute_across_cpus();

    // Allocate large dataset that might span NUMA nodes
    std::vector<int> data(data_size);
    std::iota(data.begin(), data.end(), 1);

    for (auto _ : state)
    {
        std::atomic<long long> sum{0};

        pool.parallel_for_each(data.begin(), data.end(), [&sum](int value) {
            // Memory-intensive operation
            sum.fetch_add(value * value, std::memory_order_relaxed);
        });

        benchmark::DoNotOptimize(sum.load());
    }

    state.SetItemsProcessed(state.iterations() * data_size);
    state.SetBytesProcessed(state.iterations() * data_size * sizeof(int));
}

// =============================================================================
// False Sharing Benchmarks
// =============================================================================

struct FalseSharingTest
{
    alignas(64) std::atomic<size_t> counter1{0}; // On separate cache lines
    alignas(64) std::atomic<size_t> counter2{0};
    alignas(64) std::atomic<size_t> counter3{0};
    alignas(64) std::atomic<size_t> counter4{0};
};

static void BM_FalseSharing_Avoided(benchmark::State &state)
{
    const size_t num_threads           = state.range(0);
    const size_t increments_per_thread = 100000;

    HighPerformancePool pool(num_threads);
    pool.configure_threads("false_sharing_bench");

    FalseSharingTest test_data;

    for (auto _ : state)
    {
        std::vector<std::future<void>> futures;

        for (size_t t = 0; t < num_threads; ++t)
        {
            futures.push_back(pool.submit([&test_data, t, increments_per_thread]() {
                std::atomic<size_t> *counter = nullptr;
                switch (t % 4)
                {
                case 0:
                    counter = &test_data.counter1;
                    break;
                case 1:
                    counter = &test_data.counter2;
                    break;
                case 2:
                    counter = &test_data.counter3;
                    break;
                case 3:
                    counter = &test_data.counter4;
                    break;
                }

                for (size_t i = 0; i < increments_per_thread; ++i)
                {
                    counter->fetch_add(1, std::memory_order_relaxed);
                }
            }));
        }

        for (auto &future : futures)
        {
            future.wait();
        }
    }

    state.SetItemsProcessed(state.iterations() * num_threads * increments_per_thread);
}

// =============================================================================
// Registration
// =============================================================================

BENCHMARK(BM_CacheFriendly_HighPerformancePool)
    ->Args({1})
    ->Args({2})
    ->Args({4})
    ->Args({8})
    ->Args({16})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_CacheUnfriendly_HighPerformancePool)
    ->Args({1})
    ->Args({2})
    ->Args({4})
    ->Args({8})
    ->Args({16})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_MemoryAllocation_TaskCreation)
    ->Args(
        {4,
         1000}
    )
    ->Args(
        {8,
         1000}
    )
    ->Args(
        {16,
         1000}
    )
    ->Args(
        {4,
         5000}
    )
    ->Args(
        {8,
         5000}
    )
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_NUMA_LocalMemory)->Args({1})->Args({2})->Args({4})->Args({8})->Args({16})->Unit(benchmark::kMillisecond);

BENCHMARK(BM_FalseSharing_Avoided)->Args({2})->Args({4})->Args({8})->Args({16})->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
