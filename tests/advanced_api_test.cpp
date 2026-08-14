#include <threadschedule/advanced.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <type_traits>
#include <vector>

namespace advanced = threadschedule::advanced;
using namespace std::chrono_literals;

TEST(AdvancedApi, UmbrellaExposesOptionalFacilities)
{
  static_assert(std::is_default_constructible_v<advanced::cpu_topology>);
  static_assert(std::is_default_constructible_v<advanced::error_handler>);
  static_assert(std::is_default_constructible_v<advanced::chaos_config>);
  static_assert(std::is_class_v<advanced::global_thread_pool>);
  static_assert(std::is_class_v<advanced::global_work_stealing_pool>);

  auto topology = advanced::read_topology();
  EXPECT_GE(topology.cpu_count, 1);

  std::vector<std::future<int>> futures;
  futures.push_back(std::async(std::launch::deferred, [] { return 7; }));
  auto values = advanced::when_all(futures);
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values.front(), 7);
}

TEST(AdvancedApi, CompositeRegistryMergesPortableSnapshots)
{
  threadschedule::thread_registry first;
  threadschedule::thread_registry second;
  ASSERT_TRUE(first.register_current_thread("first", "one"));
  ASSERT_TRUE(second.register_current_thread("second", "two"));

  advanced::composite_thread_registry composite;
  composite.attach(first);
  composite.attach(second);
  auto snapshot = composite.snapshot();

  ASSERT_TRUE(snapshot.has_value());
  EXPECT_EQ(snapshot->size(), 2u);
  EXPECT_EQ(composite.count(), 2u);
  EXPECT_FALSE(composite.empty());

  EXPECT_TRUE(second.unregister_current_thread());
  EXPECT_TRUE(first.unregister_current_thread());
}

TEST(AdvancedApi, NegativeNumaThreadIndexWrapsWithinNode)
{
  advanced::cpu_topology topology;
  topology.cpu_count = 3;
  topology.numa_nodes = 1;
  topology.node_to_cpus = { { 2, 4, 6 } };

  auto affinity = advanced::affinity_for_node(topology, 0, -1, 2);
  EXPECT_EQ(affinity.get_cpus(), (std::vector<int>{ 2, 6 }));
}

TEST(AdvancedApi, ErrorHandledTaskAcceptsLvalueCallable)
{
  auto handler = std::make_shared<advanced::error_handler>();
  bool ran = false;
  auto callable = [&ran] { ran = true; };

  auto task = advanced::make_error_handled_task(callable, std::move(handler));
  task();

  EXPECT_TRUE(ran);
}

template <typename Pool>
void
exercise_submitting_pool()
{
  Pool pool(1);
  auto future = pool.submit([] { return 42; });
  EXPECT_EQ(future.get(), 42);
  pool.shutdown();
}

TEST(AdvancedApi, SpecializedPoolsSubmitAndShutdown)
{
  exercise_submitting_pool<advanced::raw_thread_pool>();
  exercise_submitting_pool<advanced::work_stealing_pool>();
  exercise_submitting_pool<advanced::polling_pool>();

  advanced::lightweight_pool lightweight(1);
  std::atomic<int> value{ 0 };
  lightweight.post([&value] { value.store(7, std::memory_order_release); });
  lightweight.shutdown();
  EXPECT_EQ(value.load(std::memory_order_acquire), 7);

  advanced::inline_pool inline_pool;
  EXPECT_EQ(inline_pool.submit([] { return 9; }).get(), 9);
  inline_pool.shutdown();

  EXPECT_EQ(advanced::global_thread_pool::submit([] { return 11; }).get(), 11);
  EXPECT_EQ(advanced::global_work_stealing_pool::submit([] { return 13; }).get(), 13);
}

template <typename Pool>
void
exercise_scheduled_pool()
{
  Pool pool(1);
  std::promise<void> ran;
  auto ready = ran.get_future();
  auto task = pool.schedule_after(0ms, [&ran] { ran.set_value(); });
  ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
  task.cancel();
  pool.shutdown();
}

TEST(AdvancedApi, SpecializedScheduledPoolsDispatchCancelAndShutdown)
{
  exercise_scheduled_pool<advanced::raw_scheduled_pool>();
  exercise_scheduled_pool<advanced::scheduled_work_stealing_pool>();
  exercise_scheduled_pool<advanced::scheduled_polling_pool>();
  exercise_scheduled_pool<advanced::scheduled_lightweight_pool>();
}

#ifdef _WIN32
TEST(AdvancedApi, WindowsProfilesUseSafeDocumentedPriorities)
{
  auto const realtime = advanced::profiles::realtime();
  auto realtime_params = advanced::scheduler_parameters::create_for_policy(realtime.policy, realtime.priority);
  ASSERT_TRUE(realtime_params.has_value());
  EXPECT_EQ(realtime_params->sched_priority, THREAD_PRIORITY_HIGHEST);

  auto const throughput = advanced::profiles::throughput();
  auto throughput_params = advanced::scheduler_parameters::create_for_policy(throughput.policy, throughput.priority);
  ASSERT_TRUE(throughput_params.has_value());
  EXPECT_EQ(throughput_params->sched_priority, THREAD_PRIORITY_BELOW_NORMAL);
}
#endif
