// Cross-standard callable storage micro-benchmark.
//
// ThreadSchedule stores type-erased tasks in two intentionally distinct ways:
//
//   - detail::move_only_function<Signature, InlineSize> -- library-owned,
//     move-only task storage with SBO and heap fallback, identical in C++17
//     through C++26.
//   - detail::copyable_function<Signature> -- std::function-backed callback
//     storage used only where callbacks must be copied.
//
// This benchmark isolates the construction (including any heap allocation) and
// invocation cost of those two storage types, away from thread scheduling
// noise, so the same binary can be compiled under C++17/20/23/26 and compared.
// It answers two questions directly:
//
//   1. What does the fixed default inline capacity cost?
//   2. When does a larger inline capacity avoid a heap allocation?
//
// Written to compile as C++17 (no concepts / requires).

#include <array>
#include <benchmark/benchmark.h>
#include <cstdint>
#include <memory>
#include <threadschedule/detail/callable/copyable_function.hpp>
#include <threadschedule/detail/callable/move_only_function.hpp>
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

static void
bm_move_only_function_small(benchmark::State& state)
{
  bench_storage<detail::move_only_function<void()>, 1>(state);
}
static void
bm_move_only_function_medium(benchmark::State& state)
{
  bench_storage<detail::move_only_function<void()>, 6>(state);
}
static void
bm_move_only_function_large(benchmark::State& state)
{
  bench_storage<detail::move_only_function<void()>, 16>(state);
}

static void
bm_copyable_function_small(benchmark::State& state)
{
  bench_storage<detail::copyable_function<void()>, 1>(state);
}
static void
bm_copyable_function_medium(benchmark::State& state)
{
  bench_storage<detail::copyable_function<void()>, 6>(state);
}
static void
bm_copyable_function_large(benchmark::State& state)
{
  bench_storage<detail::copyable_function<void()>, 16>(state);
}

static void
bm_large_inline_small(benchmark::State& state)
{
  bench_storage<detail::move_only_function<void(), 56>, 1>(state);
}
static void
bm_large_inline_medium(benchmark::State& state)
{
  bench_storage<detail::move_only_function<void(), 56>, 6>(state);
}
static void
bm_large_inline_large(benchmark::State& state)
{
  bench_storage<detail::move_only_function<void(), 56>, 16>(state);
}

BENCHMARK(bm_move_only_function_small)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_move_only_function_medium)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_move_only_function_large)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_copyable_function_small)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_copyable_function_medium)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_copyable_function_large)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_large_inline_small)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_large_inline_medium)->Unit(benchmark::kNanosecond);
BENCHMARK(bm_large_inline_large)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
