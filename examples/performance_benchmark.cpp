#include <atomic>
#include <benchmark/benchmark.h>
#include <numeric>
#include <random>
#include <threadschedule/advanced/work_stealing_pool.hpp>
#include <threadschedule/thread_config.hpp>
#include <threadschedule/worker_count.hpp>
#include <vector>

using namespace threadschedule;

// =============================================================================
// advanced::work_stealing_pool submission throughput (submit with futures)
// =============================================================================

static void
bm_hp_pool_throughput(benchmark::State& state)
{
  auto const num_tasks = static_cast<size_t>(state.range(0));

  advanced::work_stealing_pool pool(worker_count{ std::thread::hardware_concurrency() });
  pool.configure_workers(thread_config{}.set_name("bench"));
  pool.distribute_workers();

  std::atomic<size_t> completed{ 0 };

  for (auto _ : state)
    {
      completed = 0;
      std::vector<std::future<void>> futures;
      futures.reserve(num_tasks);

      for (size_t i = 0; i < num_tasks; ++i)
        futures.push_back(pool.submit_or_throw([&completed]() { completed.fetch_add(1, std::memory_order_relaxed); }));

      for (auto& f : futures)
        f.wait();

      benchmark::DoNotOptimize(completed.load());
    }

  auto stats = pool.get_statistics();
  state.counters["steal_%"] = 100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1));
  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(num_tasks));
}

BENCHMARK(bm_hp_pool_throughput)->Arg(1000)->Arg(10000)->Arg(100000)->Unit(benchmark::kMicrosecond);

// =============================================================================
// advanced::work_stealing_pool batch processing
// =============================================================================

static void
bm_hp_pool_batch(benchmark::State& state)
{
  auto const batch_size = static_cast<size_t>(state.range(0));

  advanced::work_stealing_pool pool(worker_count{ std::thread::hardware_concurrency() });
  pool.configure_workers(thread_config{}.set_name("bench"));
  pool.distribute_workers();

  std::atomic<size_t> counter{ 0 };
  std::vector<std::function<void()>> tasks;
  tasks.reserve(batch_size);
  for (size_t i = 0; i < batch_size; ++i)
    tasks.emplace_back([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });

  for (auto _ : state)
    {
      auto futures = pool.submit_batch_or_throw(tasks.begin(), tasks.end());
      for (auto& f : futures)
        f.wait();
    }

  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(batch_size));
}

BENCHMARK(bm_hp_pool_batch)->Arg(5000)->Arg(50000)->Unit(benchmark::kMillisecond);

// =============================================================================
// advanced::work_stealing_pool variable workload (simulating real tasks)
// =============================================================================

static void
bm_hp_pool_variable_workload(benchmark::State& state)
{
  auto const num_tasks = static_cast<size_t>(state.range(0));

  advanced::work_stealing_pool pool(worker_count{ std::thread::hardware_concurrency() });
  pool.configure_workers(thread_config{}.set_name("bench"));
  pool.distribute_workers();

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
          futures.push_back(pool.submit_or_throw(
              [amount]()
                {
                  int x = 0;
                  for (int j = 0; j < amount; ++j)
                    x += j * j;
                  benchmark::DoNotOptimize(x);
                }));
        }

      for (auto& f : futures)
        f.wait();
    }

  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(num_tasks));
}

BENCHMARK(bm_hp_pool_variable_workload)->Arg(1000)->Arg(25000)->Unit(benchmark::kMillisecond);

// =============================================================================
// advanced::work_stealing_pool parallel_for_each
// =============================================================================

static void
bm_hp_pool_parallel_for_each(benchmark::State& state)
{
  auto const data_size = static_cast<size_t>(state.range(0));

  advanced::work_stealing_pool pool(worker_count{ std::thread::hardware_concurrency() });
  pool.configure_workers(thread_config{}.set_name("bench"));
  pool.distribute_workers();

  std::vector<int> data(data_size);
  std::iota(data.begin(), data.end(), 1);

  for (auto _ : state)
    {
      std::atomic<long long> sum{ 0 };
      pool.parallel_for_each(data.begin(), data.end(),
                             [&sum](int v) { sum.fetch_add(v * v, std::memory_order_relaxed); });
      benchmark::DoNotOptimize(sum.load());
    }

  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(data_size));
}

BENCHMARK(bm_hp_pool_parallel_for_each)->Arg(100000)->Arg(1000000)->Arg(10000000)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
