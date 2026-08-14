// Cross-standard callable storage micro-benchmark.
//
// ThreadSchedule stores type-erased tasks in one of two ways:
//
//   - detail::move_callable<Signature>  -- the hot-path storage used by
//     raw_thread_pool / polling_pool / work_stealing_pool.
//     It is an alias for std::function on C++17/20 and for
//     std::move_only_function on C++23+.
//   - detail::copyable_callable<Signature> -- the copyable callback storage
//     used by tracing, registry, and error hooks. It is an alias for
//     std::function before C++26 and for std::copyable_function on C++26.
//   - detail::sbo_callable<TaskSize>     -- the small-buffer callable used by
//     lightweight_pool. It stores callables up to TaskSize-8 bytes
//     inline and is identical across every C++ standard.
//
// This benchmark isolates the construction (including any heap allocation) and
// invocation cost of those two storage types, away from thread scheduling
// noise, so the same binary can be compiled under C++17/20/23/26 and compared.
// It answers two questions directly:
//
//   1. Does replacing std::function with std::move_only_function help?
//      -> compare BM_MoveCallable_* across standards.
//   2. Does replacing std::function with std::copyable_function help?
//      -> compare BM_CopyableCallable_* across standards.
//   3. Do the SBO callables help?
//      -> compare BM_Sbo_* against BM_MoveCallable_* for the same capture.
//
// Written to compile as C++17 (no concepts / requires).

#include <array>
#include <benchmark/benchmark.h>
#include <cstdint>
#include <memory>
#include <threadschedule/detail/callable/move_callable.hpp>
#include <threadschedule/thread_pool.hpp>
#include <vector>

using namespace threadschedule;

namespace
{

// Build kBatch callables (each capturing NWords * 8 bytes) into a reused
// vector, then invoke them all. This amortizes timer overhead and measures
// exactly the storage construction + indirect call that the callable type
// controls.
template <typename Storage, std::size_t NWords>
void
bench_storage(benchmark::State& state)
{
  constexpr std::size_t k_batch = 256;
  std::vector<Storage> store;
  store.reserve(k_batch);
  std::uint64_t sink = 0;

  for (auto _ : state)
    {
      store.clear();
      for (std::size_t i = 0; i < k_batch; ++i)
        {
          std::array<std::uint64_t, NWords> payload{};
          payload[0] = i;
          store.emplace_back([payload, &sink]() mutable { sink += payload[0] + 1; });
        }
      for (auto& callable : store)
        callable();
      benchmark::DoNotOptimize(sink);
      benchmark::ClobberMemory();
    }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * k_batch));
}

} // namespace

// move_callable == std::function (C++17/20) or std::move_only_function
// (C++23+)
static void
bm_move_callable_small(benchmark::State& state)
{
  bench_storage<detail::move_callable<void()>, 1>(state); // 8 B capture (fits all)
}
static void
bm_move_callable_medium(benchmark::State& state)
{
  bench_storage<detail::move_callable<void()>, 6>(state); // 48 B capture (heap in std lib callables)
}
static void
bm_move_callable_large(benchmark::State& state)
{
  bench_storage<detail::move_callable<void()>, 16>(state); // 128 B capture (heap everywhere)
}

// copyable_callable == std::function (pre-C++26) or std::copyable_function
// (C++26)
static void
bm_copyable_callable_small(benchmark::State& state)
{
  bench_storage<detail::copyable_callable<void()>, 1>(state); // 8 B capture (fits all)
}
static void
bm_copyable_callable_medium(benchmark::State& state)
{
  bench_storage<detail::copyable_callable<void()>, 6>(state); // 48 B capture
}
static void
bm_copyable_callable_large(benchmark::State& state)
{
  bench_storage<detail::copyable_callable<void()>, 16>(state); // 128 B capture
}

// sbo_callable<64> == lightweight_pool storage (56 B inline buffer)
static void
bm_sbo_small(benchmark::State& state)
{
  bench_storage<detail::sbo_callable<64>, 1>(state);
}
static void
bm_sbo_medium(benchmark::State& state)
{
  bench_storage<detail::sbo_callable<64>, 6>(state);
}
static void
bm_sbo_large(benchmark::State& state)
{
  bench_storage<detail::sbo_callable<64>, 16>(state);
}

BENCHMARK(bm_move_callable_small)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_move_callable_medium)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_move_callable_large)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_copyable_callable_small)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_copyable_callable_medium)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_copyable_callable_large)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_sbo_small)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_sbo_medium)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_sbo_large)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
