#include <array>
#include <atomic>
#include <benchmark/benchmark.h>
#include <memory>
#include <thread>
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

namespace
{

template <typename Pool>
void wait_for_posts(Pool& pool)
{
    if constexpr (requires { pool.wait_for_tasks(); })
    {
        pool.wait_for_tasks();
    }
}

template <typename Pool, typename SubmitFn>
void run_post_benchmark(benchmark::State& state, SubmitFn&& submit)
{
    Pool pool(static_cast<size_t>(state.range(0)));
    std::atomic<size_t> completed{0};
    size_t const task_count = static_cast<size_t>(state.range(1));

    for (auto _ : state)
    {
        completed.store(0, std::memory_order_relaxed);
        for (size_t i = 0; i < task_count; ++i)
        {
            submit(pool, completed);
        }

        while (completed.load(std::memory_order_acquire) < task_count)
            std::this_thread::yield();

        wait_for_posts(pool);
    }

    state.SetItemsProcessed(state.iterations() * task_count);
}

} // namespace

static void BM_ThreadPool_PostSmallCapture(benchmark::State& state)
{
    run_post_benchmark<ThreadPool>(state, [](ThreadPool& pool, std::atomic<size_t>& completed) {
        pool.post([&completed]() { completed.fetch_add(1, std::memory_order_relaxed); });
    });
}

static void BM_ThreadPool_PostLargeCapture(benchmark::State& state)
{
    run_post_benchmark<ThreadPool>(state, [](ThreadPool& pool, std::atomic<size_t>& completed) {
        std::array<int, 32> payload{};
        payload[0] = 7;
        pool.post([payload, &completed]() {
            completed.fetch_add(payload[0] == 7 ? 1u : 0u, std::memory_order_relaxed);
        });
    });
}

static void BM_HighPerformancePool_PostSmallCapture(benchmark::State& state)
{
    run_post_benchmark<HighPerformancePool>(state, [](HighPerformancePool& pool, std::atomic<size_t>& completed) {
        pool.post([&completed]() { completed.fetch_add(1, std::memory_order_relaxed); });
    });
}

static void BM_HighPerformancePool_PostLargeCapture(benchmark::State& state)
{
    run_post_benchmark<HighPerformancePool>(state, [](HighPerformancePool& pool, std::atomic<size_t>& completed) {
        std::array<int, 32> payload{};
        payload[0] = 11;
        pool.post([payload, &completed]() {
            completed.fetch_add(payload[0] == 11 ? 1u : 0u, std::memory_order_relaxed);
        });
    });
}

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
static void BM_ThreadPool_PostMoveOnlyCapture(benchmark::State& state)
{
    run_post_benchmark<ThreadPool>(state, [](ThreadPool& pool, std::atomic<size_t>& completed) {
        auto payload = std::make_unique<int>(123);
        pool.post([payload = std::move(payload), &completed]() mutable {
            benchmark::DoNotOptimize(*payload);
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    });
}

static void BM_HighPerformancePool_PostMoveOnlyCapture(benchmark::State& state)
{
    run_post_benchmark<HighPerformancePool>(state, [](HighPerformancePool& pool, std::atomic<size_t>& completed) {
        auto payload = std::make_unique<int>(456);
        pool.post([payload = std::move(payload), &completed]() mutable {
            benchmark::DoNotOptimize(*payload);
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    });
}
#endif

BENCHMARK(BM_ThreadPool_PostSmallCapture)->Args({2, 1000})->Args({4, 10000});
BENCHMARK(BM_ThreadPool_PostLargeCapture)->Args({2, 1000})->Args({4, 10000});
BENCHMARK(BM_HighPerformancePool_PostSmallCapture)->Args({2, 1000})->Args({4, 10000});
BENCHMARK(BM_HighPerformancePool_PostLargeCapture)->Args({2, 1000})->Args({4, 10000});

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
BENCHMARK(BM_ThreadPool_PostMoveOnlyCapture)->Args({2, 1000})->Args({4, 10000});
BENCHMARK(BM_HighPerformancePool_PostMoveOnlyCapture)->Args({2, 1000})->Args({4, 10000});
#endif

BENCHMARK_MAIN();
