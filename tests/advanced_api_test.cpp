#include <threadschedule/advanced.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#ifndef _WIN32
#  include <filesystem>
#  include <fstream>
#  include <unistd.h>
#endif

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

TEST(AdvancedApi, NativeThreadIdConversionRejectsNarrowing)
{
  auto valid = advanced::native_id(threadschedule::thread_id{ 1 });
  ASSERT_TRUE(valid.has_value());
  EXPECT_EQ(valid.value(), static_cast<advanced::native_thread_id>(1));

  auto oversized = advanced::native_id(threadschedule::thread_id{ (std::numeric_limits<std::int64_t>::max)() });
  ASSERT_FALSE(oversized.has_value());
  EXPECT_EQ(oversized.error(), std::make_error_code(std::errc::invalid_argument));
}

TEST(AdvancedApi, NegativeNumaThreadIndexWrapsWithinNode)
{
  advanced::cpu_topology topology;
  topology.cpu_count = 3;
  topology.numa_nodes = 1;
  topology.node_to_cpus = { { threadschedule::cpu_id{ 2 }, threadschedule::cpu_id{ 4 }, threadschedule::cpu_id{ 6 } } };

  auto affinity = advanced::affinity_for_node(topology, 0, -1, 2);
  EXPECT_EQ(affinity.cpus(),
            (std::vector<threadschedule::cpu_id>{ threadschedule::cpu_id{ 2 }, threadschedule::cpu_id{ 6 } }));
}

TEST(AdvancedApi, MalformedOrEmptyNumaSnapshotsReturnEmptyAffinity)
{
  advanced::cpu_topology missing_mapping;
  missing_mapping.numa_nodes = 2;
  EXPECT_TRUE(advanced::affinity_for_node(missing_mapping, 1, 0).empty());

  advanced::cpu_topology topology;
  topology.numa_nodes = 1;
  topology.node_to_cpus = { { threadschedule::cpu_id{ 2 } } };
  EXPECT_TRUE(advanced::affinity_for_node(topology, 0, 0, 0).empty());
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

TEST(AdvancedApi, FutureErrorCallbackCannotReplaceOriginalException)
{
  std::promise<int> failed;
  failed.set_exception(std::make_exception_ptr(std::runtime_error("original")));
  advanced::future_with_error_handler<int> future(failed.get_future());
  bool callback_called = false;
  future.on_error(
      [&callback_called](std::exception_ptr)
        {
          callback_called = true;
          throw std::logic_error("callback");
        });

  EXPECT_THROW((void)future.get(), std::runtime_error);
  EXPECT_TRUE(callback_called);
}

#ifndef _WIN32
TEST(AdvancedApi, CgroupAttachUsesOnlyExistingThreadControlFiles)
{
  auto const directory = std::filesystem::temp_directory_path()
                         / ("threadschedule-cgroup-" + std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(directory);
  std::filesystem::create_directory(directory);

  auto missing = advanced::cgroup_attach_tid(directory.string(), static_cast<advanced::native_thread_id>(getpid()));
  EXPECT_FALSE(missing.has_value());
  EXPECT_FALSE(std::filesystem::exists(directory / "cgroup.threads"));
  EXPECT_FALSE(std::filesystem::exists(directory / "tasks"));

  {
    std::ofstream tasks(directory / "tasks");
  }
  auto attached = advanced::cgroup_attach_tid(directory.string(), static_cast<advanced::native_thread_id>(getpid()));
  ASSERT_TRUE(attached.has_value()) << attached.error().message();
  std::ifstream tasks(directory / "tasks");
  std::string written;
  std::getline(tasks, written);
  EXPECT_EQ(written, std::to_string(getpid()));

  auto invalid = advanced::cgroup_attach_tid(directory.string(), static_cast<advanced::native_thread_id>(0));
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error(), std::make_error_code(std::errc::invalid_argument));
  std::filesystem::remove_all(directory);
}
#endif

template <typename Pool>
void
exercise_submitting_pool()
{
  Pool pool(threadschedule::worker_count{ 1 });
  auto future = pool.submit_or_throw([] { return 42; });
  EXPECT_EQ(future.get(), 42);
  pool.shutdown();
}

TEST(AdvancedApi, SpecializedPoolsSubmitAndShutdown)
{
  exercise_submitting_pool<advanced::raw_thread_pool>();
  exercise_submitting_pool<advanced::work_stealing_pool>();
  exercise_submitting_pool<advanced::polling_pool>();

  advanced::lightweight_pool lightweight(threadschedule::worker_count{ 1 });
  std::atomic<int> value{ 0 };
  lightweight.post([&value] { value.store(7, std::memory_order_release); });
  lightweight.shutdown();
  EXPECT_EQ(value.load(std::memory_order_acquire), 7);

  advanced::inline_pool inline_pool;
  EXPECT_EQ(inline_pool.submit_or_throw([] { return 9; }).get(), 9);
  std::vector<std::function<void()>> inline_tasks{ [] {}, [] {} };
  EXPECT_EQ(inline_pool.submit_batch_or_throw(inline_tasks.begin(), inline_tasks.end()).size(), 2u);
  inline_pool.shutdown();

  EXPECT_EQ(advanced::global_thread_pool::submit_or_throw([] { return 11; }).get(), 11);
  EXPECT_EQ(advanced::global_work_stealing_pool::submit_or_throw([] { return 13; }).get(), 13);
}

template <typename Pool>
void
exercise_scheduled_pool()
{
  Pool pool(threadschedule::worker_count{ 1 });
  std::promise<void> ran;
  auto ready = ran.get_future();
  auto task = pool.schedule_after(0ms, [&ran] { ran.set_value(); });
  ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
  ASSERT_TRUE(task.has_value());
  task->cancel();
  pool.shutdown();
}

TEST(AdvancedApi, SpecializedScheduledPoolsDispatchCancelAndShutdown)
{
  exercise_scheduled_pool<advanced::raw_scheduled_pool>();
  exercise_scheduled_pool<advanced::scheduled_work_stealing_pool>();
  exercise_scheduled_pool<advanced::scheduled_polling_pool>();
  exercise_scheduled_pool<advanced::scheduled_lightweight_pool>();
}

TEST(AdvancedApi, ProfilesUsePortableSchedulingIntents)
{
  auto const realtime = advanced::profiles::realtime();
  EXPECT_EQ(realtime.scheduling.intent(), threadschedule::scheduling_intent::realtime_fifo);

  auto const throughput = advanced::profiles::throughput();
  EXPECT_EQ(throughput.scheduling.intent(), threadschedule::scheduling_intent::normal);
}
