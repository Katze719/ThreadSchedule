#include <threadschedule/threadschedule.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <type_traits>

using namespace std::chrono_literals;

TEST(V3Api, CoreObjectsConstructDirectly)
{
  std::atomic<bool> ran{ false };
  threadschedule::thread worker([&ran] { ran.store(true); });
  ASSERT_TRUE(worker.join().has_value());
  EXPECT_TRUE(ran.load());

  threadschedule::thread_pool pool(2);
  auto submitted = pool.submit([] { return 42; });
  ASSERT_TRUE(submitted.has_value());
  EXPECT_EQ(submitted->get(), 42);

  threadschedule::scheduled_pool scheduler(1);
  EXPECT_EQ(scheduler.scheduled_count(), 0u);

  threadschedule::thread_registry registry;
  EXPECT_TRUE(registry.empty());
}

TEST(V3Api, ThreadConfigurationConstructor)
{
  threadschedule::thread_config config;
  config.name = "v3-direct";

  std::atomic<bool> ran{ false };
  threadschedule::thread worker(config, [&ran] { ran.store(true); });
  ASSERT_TRUE(worker.join().has_value());
  EXPECT_TRUE(ran.load());
}

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
TEST(V3Api, JThreadInjectsStopToken)
{
  std::promise<int> started;
  auto ready = started.get_future();
  threadschedule::jthread worker(
      [&started](std::stop_token token, int value)
        {
          started.set_value(value);
          while (!token.stop_requested())
            std::this_thread::yield();
        },
      42);

  EXPECT_EQ(ready.get(), 42);
  EXPECT_FALSE(worker.stop_requested());
  EXPECT_TRUE(worker.request_stop());
  EXPECT_TRUE(worker.get_stop_token().stop_requested());
  ASSERT_TRUE(worker.join().has_value());
}

TEST(V3Api, JThreadConfigurationConstructor)
{
  threadschedule::thread_config config;
  config.name = "v3-jthread";

  threadschedule::jthread worker(config, [](std::stop_token) {});
  ASSERT_TRUE(worker.join().has_value());
}
#endif

TEST(V3Api, OptionalFactoriesReturnExpected)
{
  std::atomic<bool> ran{ false };
  auto worker = threadschedule::thread::create([&ran] { ran.store(true); });
  ASSERT_TRUE(worker.has_value());
  worker->join();
  EXPECT_TRUE(ran.load());

  EXPECT_TRUE(threadschedule::thread_registry::create().has_value());
  EXPECT_TRUE(threadschedule::thread_pool::create({ 1 }).has_value());
  EXPECT_TRUE(threadschedule::scheduled_pool::create({ 1 }).has_value());
}

TEST(V3Api, PoolDefaultsToNonThrowingSubmission)
{
  threadschedule::thread_pool pool(2);

  auto submitted = pool.submit([] { return 42; });
  ASSERT_TRUE(submitted.has_value());
  EXPECT_EQ(submitted->get(), 42);

  auto posted = pool.post([] {});
  EXPECT_TRUE(posted.has_value());
  pool.wait();
}

TEST(V3Api, ScheduledPoolReportsShutdown)
{
  threadschedule::scheduled_pool scheduler(1);
  ASSERT_TRUE(scheduler.shutdown().has_value());

  auto scheduled = scheduler.schedule_after(1ms, [] {});
  ASSERT_FALSE(scheduled.has_value());
  EXPECT_EQ(scheduled.error(),
            std::make_error_code(std::errc::operation_canceled));
}

TEST(V3Api, PoolReportsTaskExceptionsAndPreservesFutureException)
{
  std::promise<std::string> reported;
  auto report = reported.get_future();
  threadschedule::thread_pool_config config;
  config.worker_count = 1;
  config.on_task_error = [&reported](threadschedule::task_error const& error)
    {
      reported.set_value(error.what());
      throw std::runtime_error("reporter failed");
    };

  threadschedule::thread_pool pool(std::move(config));
  auto submitted
      = pool.submit([]() -> int { throw std::runtime_error("boom"); });
  ASSERT_TRUE(submitted.has_value());
  EXPECT_THROW(submitted->get(), std::runtime_error);
  EXPECT_EQ(report.get(), "boom");
}

TEST(V3Api, SubmissionErrorsUseExpectedByDefault)
{
  threadschedule::thread_pool pool(1);
  ASSERT_TRUE(pool.shutdown().has_value());

  auto submitted = pool.submit([] { return 42; });
  ASSERT_FALSE(submitted.has_value());
  EXPECT_EQ(submitted.error(),
            std::make_error_code(std::errc::operation_canceled));
  EXPECT_THROW(pool.submit_or_throw([] {}), std::system_error);
}

TEST(V3Api, AdvancedPoolsRemainAvailable)
{
  static_assert(std::is_same_v<threadschedule::advanced::work_stealing_pool,
                               threadschedule::work_stealing_pool_backend>);
  threadschedule::advanced::inline_pool pool;
  EXPECT_EQ(pool.submit([] { return 7; }).get(), 7);
}

TEST(V3Api, CoreTypesAreIndependentImplementations)
{
  static_assert(!std::is_same_v<threadschedule::thread_config,
                                threadschedule::native_thread_config>);
  static_assert(!std::is_same_v<threadschedule::scheduling_config,
                                threadschedule::native_scheduling_config>);
  static_assert(!std::is_same_v<threadschedule::thread_affinity,
                                threadschedule::native_thread_affinity>);
  static_assert(!std::is_same_v<threadschedule::thread_registry,
                                threadschedule::thread_registry_backend>);
  static_assert(!std::is_base_of_v<threadschedule::detail::thread_backend,
                                   threadschedule::thread>);
  static_assert(!std::is_base_of_v<threadschedule::detail::thread_view_backend,
                                   threadschedule::thread_view>);
}

TEST(V3Api, AffinityIsANormalizedValueType)
{
  threadschedule::thread_affinity affinity({ 3, 1, 3, -1 });
  ASSERT_EQ(affinity.cpus(), (std::vector<int>{ 1, 3 }));
  affinity.add_cpu(2);
  affinity.remove_cpu(3);
  EXPECT_EQ(affinity.cpus(), (std::vector<int>{ 1, 2 }));
}

TEST(V3Api, RegistryUsesLowercaseSnapshots)
{
  threadschedule::thread_registry registry;
  ASSERT_TRUE(registry.register_current_thread("test", "v3").has_value());

  auto snapshot = registry.snapshot();
  ASSERT_TRUE(snapshot.has_value());
  ASSERT_EQ(snapshot->size(), 1u);
  EXPECT_EQ(snapshot->front().name, "test");
  EXPECT_EQ(snapshot->front().component, "v3");

  EXPECT_TRUE(registry.unregister_current_thread().has_value());
  EXPECT_TRUE(registry.empty());
}
