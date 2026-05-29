// Cross-standard callable storage micro-benchmark.
//
// ThreadSchedule stores type-erased tasks in one of two ways:
//
//   - detail::move_callable<Signature>  -- the hot-path storage used by
//     ThreadPool / FastThreadPool / HighPerformancePool. It is an alias for
//     std::function on C++17/20 and for std::move_only_function on C++23+.
//   - detail::SboCallable<TaskSize>     -- the small-buffer callable used by
//     LightweightPool. It stores callables up to TaskSize-8 bytes inline and is
//     identical across every C++ standard.
//
// This benchmark isolates the construction (including any heap allocation) and
// invocation cost of those two storage types, away from thread scheduling noise,
// so the same binary can be compiled under C++17/20/23/26 and compared. It
// answers two questions directly:
//
//   1. Does replacing std::function with std::move_only_function help?
//      -> compare BM_MoveCallable_* across standards.
//   2. Do the SBO callables help?
//      -> compare BM_Sbo_* against BM_MoveCallable_* for the same capture.
//
// Written to compile as C++17 (no concepts / requires).

#include <array>
#include <benchmark/benchmark.h>
#include <cstdint>
#include <memory>
#include <vector>
#include <threadschedule/callable.hpp>
#include <threadschedule/thread_pool.hpp>

using namespace threadschedule;

namespace
{

// Build kBatch callables (each capturing NWords * 8 bytes) into a reused vector,
// then invoke them all. This amortizes timer overhead and measures exactly the
// storage construction + indirect call that the callable type controls.
template <typename Storage, std::size_t NWords>
void bench_storage(benchmark::State& state)
{
    constexpr std::size_t kBatch = 256;
    std::vector<Storage> store;
    store.reserve(kBatch);
    volatile std::uint64_t sink = 0;

    for (auto _ : state)
    {
        store.clear();
        for (std::size_t i = 0; i < kBatch; ++i)
        {
            std::array<std::uint64_t, NWords> payload{};
            payload[0] = i;
            store.emplace_back([payload, &sink]() mutable { sink += payload[0] + 1; });
        }
        for (auto& callable : store)
            callable();
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * kBatch));
}

} // namespace

// move_callable == std::function (C++17/20) or std::move_only_function (C++23+)
static void BM_MoveCallable_Small(benchmark::State& state)
{
    bench_storage<detail::move_callable<void()>, 1>(state); // 8 B capture (fits all)
}
static void BM_MoveCallable_Medium(benchmark::State& state)
{
    bench_storage<detail::move_callable<void()>, 6>(state); // 48 B capture (heap in std lib callables)
}
static void BM_MoveCallable_Large(benchmark::State& state)
{
    bench_storage<detail::move_callable<void()>, 16>(state); // 128 B capture (heap everywhere)
}

// SboCallable<64> == LightweightPool storage (56 B inline buffer)
static void BM_Sbo_Small(benchmark::State& state)
{
    bench_storage<detail::SboCallable<64>, 1>(state);
}
static void BM_Sbo_Medium(benchmark::State& state)
{
    bench_storage<detail::SboCallable<64>, 6>(state);
}
static void BM_Sbo_Large(benchmark::State& state)
{
    bench_storage<detail::SboCallable<64>, 16>(state);
}

BENCHMARK(BM_MoveCallable_Small)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_MoveCallable_Medium)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_MoveCallable_Large)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Sbo_Small)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Sbo_Medium)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Sbo_Large)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
