#include <threadschedule/advanced.hpp>
#include <threadschedule/threadschedule.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>

#ifndef _WIN32
#  include <sys/resource.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#endif

using namespace std::chrono_literals;

template <typename T, typename = void>
struct has_native_handle_member : std::false_type
{
};

template <typename T>
struct has_native_handle_member<T, std::void_t<decltype(std::declval<T&>().native_handle())>> : std::true_type
{
};

TEST(V3Api, CoreObjectsConstructDirectly)
{
  std::atomic<bool> ran{ false };
  threadschedule::thread worker([&ran] { ran.store(true); });
  ASSERT_TRUE(worker.join().has_value());
  EXPECT_TRUE(ran.load());

  threadschedule::thread_pool pool(threadschedule::worker_count{ 2 });
  auto submitted = pool.submit([] { return 42; });
  ASSERT_TRUE(submitted.has_value());
  EXPECT_EQ(submitted->get(), 42);

  threadschedule::scheduled_pool scheduler(threadschedule::worker_count{ 1 });
  EXPECT_EQ(scheduler.scheduled_count(), 0u);

  threadschedule::thread_registry registry;
  EXPECT_TRUE(registry.empty());
}

TEST(V3Api, ThreadConfigurationConstructor)
{
  threadschedule::thread_config config;
  config.set_name("v3-direct");

  std::atomic<bool> ran{ false };
  threadschedule::thread worker(config, [&ran] { ran.store(true); });
  ASSERT_TRUE(worker.join().has_value());
  EXPECT_TRUE(ran.load());
}

TEST(V3Api, ThreadConfigurationIsAnExplicitPatch)
{
  threadschedule::thread_config config;
  EXPECT_TRUE(config.empty());

  config.set_name("");
  ASSERT_TRUE(config.name().has_value());
  EXPECT_TRUE(config.name()->empty());
  EXPECT_FALSE(config.scheduling().has_value());
  EXPECT_FALSE(config.affinity().has_value());

  config.clear_name();
  EXPECT_TRUE(config.empty());
  config.set_scheduling(threadschedule::schedule::background());
  EXPECT_FALSE(config.name().has_value());
  EXPECT_TRUE(config.scheduling().has_value());
  EXPECT_FALSE(config.affinity().has_value());
}

TEST(V3Api, ClosedConfigurationTypesAreNotAggregates)
{
  static_assert(!std::is_aggregate_v<threadschedule::scheduling_config>);
  static_assert(!std::is_aggregate_v<threadschedule::thread_config>);
  static_assert(!std::is_aggregate_v<threadschedule::thread_pool_config>);
  static_assert(!std::is_aggregate_v<threadschedule::scheduled_pool_config>);
}

TEST(V3Api, PortablePriorityFactoriesExposeLevelsAndNiceValues)
{
  constexpr auto high = threadschedule::schedule::priority(threadschedule::priority_level::high);
  constexpr auto nice = threadschedule::schedule::nice(threadschedule::nice_value{ 10 });

  static_assert(high.intent() == threadschedule::scheduling_intent::nice);
  static_assert(high.priority_value() == -5);
  static_assert(nice.intent() == threadschedule::scheduling_intent::nice);
  static_assert(nice.priority_value() == 10);
}

TEST(V3Api, ThreadSetsAndReadsPortablePriority)
{
  std::promise<void> release;
  auto ready = release.get_future().share();
  threadschedule::thread worker([ready] { ready.wait(); });

  auto set = worker.set_priority(threadschedule::priority_level::low);
  auto priority = worker.get_priority();
  release.set_value();
  auto joined = worker.join();

  ASSERT_TRUE(set.has_value()) << set.error().message();
  ASSERT_TRUE(priority.has_value()) << priority.error().message();
  EXPECT_EQ(priority.value(), threadschedule::priority_level::low);
  EXPECT_TRUE(joined.has_value());
}

TEST(V3Api, ThisThreadReadsAndReappliesAffinity)
{
  auto original = threadschedule::this_thread::get_affinity();
  ASSERT_TRUE(original.has_value()) << original.error().message();
  ASSERT_FALSE(original->empty());

  auto applied = threadschedule::this_thread::set_affinity(*original);
  ASSERT_TRUE(applied.has_value()) << applied.error().message();

  auto effective = threadschedule::this_thread::get_affinity();
  ASSERT_TRUE(effective.has_value()) << effective.error().message();
  EXPECT_EQ(effective->cpus(), original->cpus());
}

TEST(V3Api, StrongSchedulingValuesRejectInvalidConstruction)
{
  EXPECT_TRUE(threadschedule::this_thread::get_priority().has_value());

  EXPECT_THROW((void)threadschedule::nice_value{ 20 }, std::invalid_argument);
  EXPECT_THROW((void)threadschedule::realtime_priority{ 0 }, std::invalid_argument);
  EXPECT_THROW((void)threadschedule::realtime_priority{ 100 }, std::invalid_argument);

  auto invalid_nice = threadschedule::nice_value::create(20);
  ASSERT_FALSE(invalid_nice.has_value());
  EXPECT_EQ(invalid_nice.error(), std::make_error_code(std::errc::invalid_argument));
}

TEST(V3Api, InvalidNiceValueCannotFormAConfiguration)
{
  auto value = threadschedule::nice_value::create(20);
  ASSERT_FALSE(value.has_value());
  EXPECT_EQ(value.error(), std::make_error_code(std::errc::invalid_argument));
}

TEST(V3Api, InvalidRealtimePriorityCannotFormAConfiguration)
{
  std::array<int, 4> const invalid{ -1, 0, 100, 101 };
  for (auto const priority : invalid)
    {
      auto value = threadschedule::realtime_priority::create(priority);
      ASSERT_FALSE(value.has_value());
      EXPECT_EQ(value.error(), std::make_error_code(std::errc::invalid_argument));
    }
}

#ifndef _WIN32
TEST(V3Api, ConfiguredThreadObservesExactLinuxNiceValueBeforeCallable)
{
  threadschedule::thread_config config;
  config.set_scheduling(threadschedule::schedule::nice(threadschedule::nice_value{ 10 }));
  std::promise<int> observed;

  threadschedule::thread worker(config,
                                [&observed]
                                  {
                                    errno = 0;
                                    observed.set_value(getpriority(PRIO_PROCESS, 0));
                                  });

  EXPECT_EQ(observed.get_future().get(), 10);
  ASSERT_TRUE(worker.join().has_value());
}

TEST(V3Api, ThreadViewWithoutPortableIdentityRejectsLinuxNiceControl)
{
  std::promise<std::uint64_t> started;
  std::promise<void> release;
  auto ready = release.get_future().share();
  std::thread value(
      [&started, ready]
        {
          started.set_value(static_cast<std::uint64_t>(syscall(SYS_gettid)));
          ready.wait();
        });
  (void)started.get_future().get();

  threadschedule::thread_view unknown(value);
  auto unsupported = unknown.set_nice(threadschedule::nice_value{ 10 });

  release.set_value();
  value.join();

  ASSERT_FALSE(unsupported.has_value());
  EXPECT_EQ(unsupported.error(), std::make_error_code(std::errc::operation_not_supported));
}
#endif

TEST(V3Api, EmptyThreadReportsJoinAndDetachErrors)
{
  threadschedule::thread worker;

  auto joined = worker.join();
  ASSERT_FALSE(joined.has_value());
  EXPECT_EQ(joined.error(), std::make_error_code(std::errc::invalid_argument));

  auto detached = worker.detach();
  ASSERT_FALSE(detached.has_value());
  EXPECT_EQ(detached.error(), std::make_error_code(std::errc::invalid_argument));
  EXPECT_THROW(worker.join_or_throw(), std::system_error);
  EXPECT_THROW(worker.detach_or_throw(), std::system_error);
}

#ifndef _WIN32
TEST(V3Api, FailedThreadConfigurationDoesNotRunCallable)
{
  threadschedule::thread_config config;
  config.set_name("this-name-is-longer-than-fifteen-characters");
  std::atomic<bool> ran{ false };

  auto worker = threadschedule::thread::create(config, [&ran] { ran.store(true); });

  ASSERT_FALSE(worker.has_value());
  EXPECT_EQ(worker.error(), std::make_error_code(std::errc::invalid_argument));
  EXPECT_FALSE(ran.load());
}
#endif

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

TEST(V3Api, JThreadForwardsMoveOnlyArgumentWithoutStopToken)
{
  std::promise<int> observed;
  auto ready = observed.get_future();
  threadschedule::jthread worker([&observed](std::unique_ptr<int> value) { observed.set_value(*value); },
                                 std::make_unique<int>(42));

  EXPECT_EQ(ready.get(), 42);
  ASSERT_TRUE(worker.join().has_value());
}

TEST(V3Api, EmptyJThreadReportsJoinAndDetachErrors)
{
  threadschedule::jthread worker;

  auto joined = worker.join();
  ASSERT_FALSE(joined.has_value());
  EXPECT_EQ(joined.error(), std::make_error_code(std::errc::invalid_argument));

  auto detached = worker.detach();
  ASSERT_FALSE(detached.has_value());
  EXPECT_EQ(detached.error(), std::make_error_code(std::errc::invalid_argument));
  EXPECT_THROW(worker.join_or_throw(), std::system_error);
  EXPECT_THROW(worker.detach_or_throw(), std::system_error);
}

TEST(V3Api, JThreadConfigurationConstructor)
{
  threadschedule::thread_config config;
  config.set_name("v3-jthread");

  threadschedule::jthread worker(config, [](std::stop_token) {});
  ASSERT_TRUE(worker.join().has_value());
}

#  ifndef _WIN32
TEST(V3Api, ConfiguredJThreadObservesExactLinuxNiceValue)
{
  threadschedule::thread_config config;
  config.set_scheduling(threadschedule::schedule::nice(threadschedule::nice_value{ 10 }));
  std::promise<int> observed;

  threadschedule::jthread worker(config,
                                 [&observed](std::stop_token)
                                   {
                                     errno = 0;
                                     observed.set_value(getpriority(PRIO_PROCESS, 0));
                                   });

  EXPECT_EQ(observed.get_future().get(), 10);
  ASSERT_TRUE(worker.join().has_value());
}
#  endif

#  ifndef _WIN32
TEST(V3Api, FailedJThreadConfigurationDoesNotRunCallable)
{
  threadschedule::thread_config config;
  config.set_name("this-name-is-longer-than-fifteen-characters");
  std::atomic<bool> ran{ false };

  auto worker = threadschedule::jthread::create(config, [&ran](std::stop_token) { ran.store(true); });

  ASSERT_FALSE(worker.has_value());
  EXPECT_EQ(worker.error(), std::make_error_code(std::errc::invalid_argument));
  EXPECT_FALSE(ran.load());
}
#  endif
#endif

TEST(V3Api, OptionalFactoriesReturnExpected)
{
  std::atomic<bool> ran{ false };
  auto worker = threadschedule::thread::create([&ran] { ran.store(true); });
  ASSERT_TRUE(worker.has_value());
  worker->join();
  EXPECT_TRUE(ran.load());

  EXPECT_TRUE(threadschedule::thread_registry::create().has_value());
  threadschedule::thread_pool_config pool_config;
  pool_config.set_worker_count(threadschedule::worker_count{ 1 });
  EXPECT_TRUE(threadschedule::thread_pool::create(pool_config).has_value());
  threadschedule::scheduled_pool_config scheduled_config;
  scheduled_config.set_worker_count(threadschedule::worker_count{ 1 });
  EXPECT_TRUE(threadschedule::scheduled_pool::create(scheduled_config).has_value());
}

TEST(V3Api, PoolDefaultsToNonThrowingSubmission)
{
  threadschedule::thread_pool pool(threadschedule::worker_count{ 2 });

  auto submitted = pool.submit([] { return 42; });
  ASSERT_TRUE(submitted.has_value());
  EXPECT_EQ(submitted->get(), 42);

  auto posted = pool.post([] {});
  EXPECT_TRUE(posted.has_value());
  pool.wait();
}

TEST(V3Api, MovedFromPoolsRemainSafe)
{
  auto expect_stopped = [](threadschedule::thread_pool& pool)
    {
      EXPECT_EQ(pool.size(), 0u);

      auto submitted = pool.submit([] { return 1; });
      ASSERT_FALSE(submitted.has_value());
      EXPECT_EQ(submitted.error(), std::make_error_code(std::errc::operation_canceled));

      auto posted = pool.post([] {});
      ASSERT_FALSE(posted.has_value());
      EXPECT_EQ(posted.error(), std::make_error_code(std::errc::operation_canceled));

      auto waited = pool.wait();
      ASSERT_FALSE(waited.has_value());
      EXPECT_EQ(waited.error(), std::make_error_code(std::errc::operation_canceled));

      threadschedule::thread_config config;
      auto configured = pool.configure_workers(config);
      ASSERT_FALSE(configured.has_value());
      EXPECT_EQ(configured.error(), std::make_error_code(std::errc::operation_canceled));

      try
        {
          pool.wait_or_throw();
          FAIL() << "wait_or_throw should reject a moved-from pool";
        }
      catch (std::system_error const& error)
        {
          EXPECT_EQ(error.code(), std::make_error_code(std::errc::operation_canceled));
        }

      EXPECT_TRUE(pool.shutdown().has_value());
    };

  threadschedule::thread_pool source(threadschedule::worker_count{ 1 });
  threadschedule::thread_pool moved(std::move(source));
  expect_stopped(source);

  auto first = moved.submit([] { return 41; });
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->get(), 41);

  threadschedule::thread_pool assigned(threadschedule::worker_count{ 1 });
  assigned = std::move(moved);
  expect_stopped(moved);

  auto second = assigned.submit([] { return 42; });
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->get(), 42);
}

TEST(V3Api, PoolMoveAssignmentUsesDestinationShutdownPolicy)
{
  threadschedule::thread_pool_config config;
  config.set_worker_count(threadschedule::worker_count{ 1 })
      .set_shutdown_policy(threadschedule::shutdown_policy::drop_pending);
  threadschedule::thread_pool destination(config);

  std::promise<void> entered;
  auto entered_future = entered.get_future();
  std::promise<void> release;
  auto release_future = release.get_future().share();
  ASSERT_TRUE(destination
                  .post(
                      [&entered, release_future]
                        {
                          entered.set_value();
                          release_future.wait();
                        })
                  .has_value());
  if (entered_future.wait_for(2s) != std::future_status::ready)
    {
      release.set_value();
      FAIL() << "destination worker did not start";
    }

  std::atomic<int> queued_runs{ 0 };
  bool queued = true;
  for (int i = 0; i < 4; ++i)
    {
      auto posted = destination.post([&queued_runs] { ++queued_runs; });
      queued = queued && posted.has_value();
    }

  threadschedule::thread_pool source(threadschedule::worker_count{ 1 });
  auto assignment = std::async(std::launch::async, [&destination, &source] { destination = std::move(source); });

  EXPECT_EQ(assignment.wait_for(20ms), std::future_status::timeout);
  release.set_value();
  EXPECT_EQ(assignment.wait_for(2s), std::future_status::ready);
  assignment.get();

  EXPECT_TRUE(queued);
  EXPECT_EQ(queued_runs.load(), 0);
}

#ifndef _WIN32
TEST(V3Api, PoolWorkerNamesReserveSpaceForGeneratedSuffix)
{
  threadschedule::thread_pool_config pool_config;
  threadschedule::thread_config pool_workers;
  pool_workers.set_name("123456789012345");
  pool_config.set_worker_count(threadschedule::worker_count{ 12 }).set_worker_config(std::move(pool_workers));
  auto pool = threadschedule::thread_pool::create(std::move(pool_config));
  ASSERT_TRUE(pool.has_value()) << pool.error().message();

  threadschedule::scheduled_pool_config scheduled_config;
  threadschedule::thread_config scheduled_workers;
  scheduled_workers.set_name("1234567890123");
  scheduled_config.set_worker_count(threadschedule::worker_count{ 12 }).set_worker_config(std::move(scheduled_workers));
  auto scheduler = threadschedule::scheduled_pool::create(std::move(scheduled_config));
  ASSERT_TRUE(scheduler.has_value()) << scheduler.error().message();
}
#endif

TEST(V3Api, ScheduledPoolReportsShutdown)
{
  threadschedule::scheduled_pool scheduler(threadschedule::worker_count{ 1 });
  ASSERT_TRUE(scheduler.shutdown().has_value());

  auto scheduled = scheduler.schedule_after(1ms, [] {});
  ASSERT_FALSE(scheduled.has_value());
  EXPECT_EQ(scheduled.error(), std::make_error_code(std::errc::operation_canceled));
}

TEST(V3Api, ScheduledWorkerShutdownFailureDoesNotStopPool)
{
  threadschedule::scheduled_pool scheduler(threadschedule::worker_count{ 1 });
  std::promise<std::error_code> attempted;
  auto ready = attempted.get_future();
  auto first = scheduler.schedule_after(1ms,
                                        [&]
                                          {
                                            auto result = scheduler.shutdown();
                                            attempted.set_value(result ? std::error_code{} : result.error());
                                          });
  ASSERT_TRUE(first.has_value());

  ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(ready.get(), std::make_error_code(std::errc::resource_deadlock_would_occur));

  std::promise<void> ran;
  auto ran_ready = ran.get_future();
  auto second = scheduler.schedule_after(1ms, [&ran] { ran.set_value(); });
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(ran_ready.wait_for(2s), std::future_status::ready);
  EXPECT_TRUE(scheduler.shutdown().has_value());
}

TEST(V3Api, ScheduledPoolValidatesPeriodicIntervals)
{
  threadschedule::scheduled_pool scheduler(threadschedule::worker_count{ 1 });

  auto zero = scheduler.schedule_periodic(0ms, [] {});
  ASSERT_FALSE(zero.has_value());
  EXPECT_EQ(zero.error(), std::make_error_code(std::errc::invalid_argument));

  auto negative = scheduler.schedule_periodic_after(1ms, -1ms, [] {});
  ASSERT_FALSE(negative.has_value());
  EXPECT_EQ(negative.error(), std::make_error_code(std::errc::invalid_argument));
}

TEST(V3Api, ScheduledPoolSupportsDelayedPeriodicTasks)
{
  threadschedule::scheduled_pool scheduler(threadschedule::worker_count{ 1 });
  std::promise<void> first_run;
  auto ready = first_run.get_future();
  std::atomic<bool> reported{ false };

  auto scheduled = scheduler.schedule_periodic_after(40ms, 20ms,
                                                     [&first_run, &reported]
                                                       {
                                                         if (!reported.exchange(true))
                                                           first_run.set_value();
                                                       });

  ASSERT_TRUE(scheduled.has_value());
  EXPECT_EQ(ready.wait_for(10ms), std::future_status::timeout);
  EXPECT_EQ(ready.wait_for(500ms), std::future_status::ready);
  scheduled->cancel();
}

TEST(V3Api, ScheduledPoolSupportsMoveOnlyPeriodicCallables)
{
  threadschedule::scheduled_pool scheduler(threadschedule::worker_count{ 1 });
  std::promise<int> immediate_run;
  auto immediate_ready = immediate_run.get_future();
  std::promise<int> delayed_run;
  auto delayed_ready = delayed_run.get_future();

  auto immediate
      = scheduler.schedule_periodic(20ms,
                                    [payload = std::make_unique<int>(41), &immediate_run, reported = false]() mutable
                                      {
                                        if (!reported)
                                          {
                                            reported = true;
                                            immediate_run.set_value(*payload);
                                          }
                                      });
  auto delayed = scheduler.schedule_periodic_after(
      10ms, 20ms,
      [payload = std::make_unique<int>(42), &delayed_run, reported = false]() mutable
        {
          if (!reported)
            {
              reported = true;
              delayed_run.set_value(*payload);
            }
        });

  ASSERT_TRUE(immediate.has_value());
  ASSERT_TRUE(delayed.has_value());
  ASSERT_EQ(immediate_ready.wait_for(2s), std::future_status::ready);
  ASSERT_EQ(delayed_ready.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(immediate_ready.get(), 41);
  EXPECT_EQ(delayed_ready.get(), 42);
  immediate->cancel();
  delayed->cancel();
}

TEST(V3Api, ScheduledPoolReportsTaskExceptions)
{
  std::promise<std::string> reported;
  auto report = reported.get_future();
  threadschedule::scheduled_pool_config config;
  threadschedule::thread_config worker_config;
  worker_config.set_name("v3-worker");
  threadschedule::thread_config scheduler_config;
  scheduler_config.set_name("v3-scheduler");
  config.set_worker_count(threadschedule::worker_count{ 1 })
      .set_worker_config(std::move(worker_config))
      .set_scheduler_config(std::move(scheduler_config))
      .set_error_callback([&reported](threadschedule::task_error const& error) { reported.set_value(error.what()); });

  threadschedule::scheduled_pool scheduler(std::move(config));
  auto scheduled = scheduler.schedule_after(0ms, [] { throw std::runtime_error("scheduled boom"); });

  ASSERT_TRUE(scheduled.has_value());
  EXPECT_EQ(report.get(), "scheduled boom");
}

TEST(V3Api, PoolReportsTaskExceptionsAndPreservesFutureException)
{
  std::promise<std::string> reported;
  auto report = reported.get_future();
  threadschedule::thread_pool_config config;
  config.set_worker_count(threadschedule::worker_count{ 1 })
      .set_error_callback(
          [&reported](threadschedule::task_error const& error)
            {
              reported.set_value(error.what());
              throw std::runtime_error("reporter failed");
            });

  threadschedule::thread_pool pool(std::move(config));
  auto submitted = pool.submit([]() -> int { throw std::runtime_error("boom"); });
  ASSERT_TRUE(submitted.has_value());
  EXPECT_THROW(submitted->get(), std::runtime_error);
  EXPECT_EQ(report.get(), "boom");
}

TEST(V3Api, SubmissionErrorsUseExpectedByDefault)
{
  threadschedule::thread_pool pool(threadschedule::worker_count{ 1 });
  ASSERT_TRUE(pool.shutdown().has_value());

  auto submitted = pool.submit([] { return 42; });
  ASSERT_FALSE(submitted.has_value());
  EXPECT_EQ(submitted.error(), std::make_error_code(std::errc::operation_canceled));
  EXPECT_THROW(pool.submit_or_throw([] {}), std::system_error);
}

TEST(V3Api, AdvancedPoolsRemainAvailable)
{
  static_assert(std::is_constructible_v<threadschedule::advanced::work_stealing_pool, std::size_t>);
  threadschedule::advanced::inline_pool pool;
  EXPECT_EQ(pool.submit([] { return 7; }).get(), 7);
}

TEST(V3Api, NativeHandleIsAnAdvancedEscapeHatch)
{
  static_assert(!has_native_handle_member<threadschedule::thread>::value);

  threadschedule::thread worker([] {});
  [[maybe_unused]] auto handle = threadschedule::advanced::native_handle(worker);
  ASSERT_TRUE(worker.join().has_value());

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
  static_assert(!has_native_handle_member<threadschedule::jthread>::value);
  threadschedule::jthread cancellable([](std::stop_token) {});
  [[maybe_unused]] auto cancellable_handle = threadschedule::advanced::native_handle(cancellable);
  ASSERT_TRUE(cancellable.join().has_value());
#endif
}

TEST(V3Api, CoreTypesAreIndependentImplementations)
{
  static_assert(std::is_standard_layout_v<threadschedule::thread_config>);
  static_assert(std::is_standard_layout_v<threadschedule::scheduling_config>);
  static_assert(std::is_move_constructible_v<threadschedule::thread_affinity>);
  static_assert(std::is_move_constructible_v<threadschedule::thread_registry>);
}

TEST(V3Api, AffinityIsANormalizedValueType)
{
  EXPECT_THROW((void)threadschedule::cpu_id{ -1 }, std::invalid_argument);
  threadschedule::thread_affinity affinity(
      { threadschedule::cpu_id{ 3 }, threadschedule::cpu_id{ 1 }, threadschedule::cpu_id{ 3 } });
  ASSERT_EQ(affinity.cpus(),
            (std::vector<threadschedule::cpu_id>{ threadschedule::cpu_id{ 1 }, threadschedule::cpu_id{ 3 } }));
  affinity.add_cpu(threadschedule::cpu_id{ 2 });
  affinity.remove_cpu(threadschedule::cpu_id{ 3 });
  EXPECT_EQ(affinity.cpus(),
            (std::vector<threadschedule::cpu_id>{ threadschedule::cpu_id{ 1 }, threadschedule::cpu_id{ 2 } }));
}

TEST(V3Api, AffinityReadPreservesResultContract)
{
  std::promise<void> release;
  auto ready = release.get_future().share();
  threadschedule::thread worker([ready] { ready.wait(); });

  static_assert(
      std::is_same_v<decltype(worker.get_affinity()), threadschedule::result<threadschedule::thread_affinity>>);
  auto affinity = worker.get_affinity();

  release.set_value();
  ASSERT_TRUE(worker.join().has_value());
  ASSERT_TRUE(affinity.has_value()) << affinity.error().message();
  EXPECT_FALSE(affinity->empty());
}

TEST(V3Api, AffinityRejectsPartiallyRepresentableMasks)
{
#ifdef _WIN32
  constexpr int unsupported_cpu = 64;
#else
  constexpr int unsupported_cpu = CPU_SETSIZE;
#endif
  threadschedule::thread_affinity affinity({ threadschedule::cpu_id{ 0 }, threadschedule::cpu_id{ unsupported_cpu } });
  std::promise<void> release;
  auto ready = release.get_future().share();
  threadschedule::thread worker([ready] { ready.wait(); });

  auto configured = worker.set_affinity(affinity);

  release.set_value();
  ASSERT_TRUE(worker.join().has_value());
  ASSERT_FALSE(configured.has_value());
  EXPECT_EQ(configured.error(), std::make_error_code(std::errc::invalid_argument));
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

  threadschedule::thread_config config;
  config.set_name("v3-control");
  EXPECT_TRUE(registry.configure(snapshot->front().id, config).has_value());

  EXPECT_TRUE(registry.unregister_current_thread().has_value());
  EXPECT_TRUE(registry.empty());
}

TEST(V3Api, MovedFromRegistriesRemainSafe)
{
  threadschedule::thread_registry source;
  ASSERT_TRUE(source.register_current_thread("original", "source").has_value());

  threadschedule::thread_registry moved(std::move(source));
  // Intentionally verify the documented moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_TRUE(source.empty());
  EXPECT_EQ(source.count(), 0u);
  auto source_snapshot = source.snapshot();
  ASSERT_TRUE(source_snapshot.has_value());
  EXPECT_TRUE(source_snapshot->empty());

  auto source_registration = source.register_current_thread("reused", "source");
  ASSERT_FALSE(source_registration.has_value());
  EXPECT_EQ(source_registration.error(), std::make_error_code(std::errc::operation_canceled));

  source = threadschedule::thread_registry{};
  ASSERT_TRUE(source.register_current_thread("reused", "source").has_value());
  EXPECT_EQ(source.count(), 1u);
  ASSERT_TRUE(source.unregister_current_thread().has_value());

  threadschedule::thread_registry assigned;
  assigned = std::move(moved);
  // Intentionally verify the documented moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_TRUE(moved.empty());
  EXPECT_EQ(moved.count(), 0u);
  auto moved_snapshot = moved.snapshot();
  ASSERT_TRUE(moved_snapshot.has_value());
  EXPECT_TRUE(moved_snapshot->empty());

  auto moved_registration = moved.register_current_thread("reused", "moved");
  ASSERT_FALSE(moved_registration.has_value());
  EXPECT_EQ(moved_registration.error(), std::make_error_code(std::errc::operation_canceled));

  moved = threadschedule::thread_registry{};
  ASSERT_TRUE(moved.register_current_thread("reused", "moved").has_value());
  EXPECT_EQ(moved.count(), 1u);
  ASSERT_TRUE(moved.unregister_current_thread().has_value());

  auto assigned_snapshot = assigned.snapshot();
  ASSERT_TRUE(assigned_snapshot.has_value());
  ASSERT_EQ(assigned_snapshot->size(), 1u);
  EXPECT_EQ(assigned_snapshot->front().name, "original");
  ASSERT_TRUE(assigned.unregister_current_thread().has_value());
}

TEST(V3Api, MoveAssigningInjectedRegistryRetargetsGlobalRegistry)
{
  threadschedule::thread_registry injected;
  threadschedule::thread_registry replacement;
  ASSERT_TRUE(replacement.register_current_thread("replacement", "v3").has_value());

  threadschedule::use_global_registry(&injected);
  injected = std::move(replacement);
  auto snapshot = threadschedule::global_registry().snapshot();
  threadschedule::use_global_registry(nullptr);

  ASSERT_TRUE(snapshot.has_value());
  ASSERT_EQ(snapshot->size(), 1u);
  EXPECT_EQ(snapshot->front().name, "replacement");
  EXPECT_EQ(snapshot->front().component, "v3");
  EXPECT_TRUE(injected.unregister_current_thread().has_value());
}

TEST(V3Api, MoveAssigningGlobalRegistryPreservesGlobalFacade)
{
  threadschedule::use_global_registry(nullptr);
  (void)threadschedule::global_registry().unregister_current_thread();

  threadschedule::thread_registry local;
  ASSERT_TRUE(local.register_current_thread("moved-global", "v3").has_value());
  threadschedule::global_registry() = std::move(local);

  EXPECT_EQ(threadschedule::global_registry().count(), 1u);
  EXPECT_EQ(threadschedule::global_registry().count(), 1u);
  ASSERT_TRUE(threadschedule::global_registry().unregister_current_thread().has_value());
  {
    threadschedule::auto_register_current_thread guard("global-guard", "v3");
    EXPECT_EQ(threadschedule::global_registry().count(), 1u);
    EXPECT_EQ(threadschedule::global_registry().count(), 1u);
  }

  threadschedule::use_global_registry(nullptr);
}

TEST(V3Api, DestroyingInjectedRegistryRestoresDefaultRegistry)
{
  {
    threadschedule::thread_registry injected;
    threadschedule::use_global_registry(&injected);
    ASSERT_TRUE(threadschedule::global_registry().register_current_thread("injected", "v3").has_value());
  }

  auto registered = threadschedule::global_registry().register_current_thread("default", "v3");
  ASSERT_TRUE(registered.has_value());
  EXPECT_TRUE(threadschedule::global_registry().unregister_current_thread().has_value());
}

TEST(V3Api, MovingInjectedRegistryPreservesActiveGuardBackend)
{
  threadschedule::thread_registry injected;
  threadschedule::use_global_registry(&injected);
  std::promise<void> registered;
  std::promise<void> release;
  auto release_ready = release.get_future().share();
  std::thread worker(
      [&]
        {
          threadschedule::auto_register_current_thread guard("moving", "v3");
          registered.set_value();
          release_ready.wait();
        });
  registered.get_future().wait();

  threadschedule::thread_registry replacement;
  EXPECT_NO_THROW(injected = std::move(replacement));
  release.set_value();
  worker.join();

  auto snapshot = threadschedule::global_registry().snapshot();
  threadschedule::use_global_registry(nullptr);
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_TRUE(snapshot->empty());
}

TEST(V3Api, MovingEmptyInjectedRegistryPreservesActiveGuardBackend)
{
  threadschedule::thread_registry injected;
  threadschedule::use_global_registry(&injected);
  {
    threadschedule::auto_register_current_thread guard("moving-empty", "v3");
    ASSERT_TRUE(threadschedule::global_registry().unregister_current_thread().has_value());
    EXPECT_TRUE(injected.empty());

    threadschedule::thread_registry replacement;
    EXPECT_NO_THROW(injected = std::move(replacement));
  }

  auto snapshot = threadschedule::global_registry().snapshot();
  threadschedule::use_global_registry(nullptr);
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_TRUE(snapshot->empty());
}

TEST(V3Api, RegistrySetsAndReadsPortablePriority)
{
  threadschedule::thread_registry registry;
  std::promise<bool> registered;
  std::promise<void> release;
  auto ready = release.get_future().share();
  threadschedule::thread worker(
      [&registry, &registered, ready]
        {
          registered.set_value(registry.register_current_thread("priority", "v3").has_value());
          ready.wait();
          (void)registry.unregister_current_thread();
        });

  bool const registered_ok = registered.get_future().get();
  auto snapshot = registry.snapshot();
  threadschedule::result<void> set = threadschedule::unexpected(std::make_error_code(std::errc::no_such_process));
  threadschedule::result<threadschedule::priority_level> priority
      = threadschedule::unexpected(std::make_error_code(std::errc::no_such_process));
  if (registered_ok && snapshot.has_value() && snapshot->size() == 1)
    {
      auto const id = snapshot->front().id;
      set = registry.set_nice(id, threadschedule::nice_value{ 10 });
      priority = registry.get_priority(id);
    }
  release.set_value();
  auto joined = worker.join();

  ASSERT_TRUE(registered_ok);
  ASSERT_TRUE(snapshot.has_value());
  ASSERT_EQ(snapshot->size(), 1u);
  ASSERT_TRUE(set.has_value()) << set.error().message();
  ASSERT_TRUE(priority.has_value()) << priority.error().message();
  EXPECT_EQ(priority.value(), threadschedule::priority_level::lowest);
  EXPECT_TRUE(joined.has_value());
}

#ifndef _WIN32
TEST(V3Api, PoolWorkersReceiveExactLinuxNiceValue)
{
  threadschedule::thread_pool_config config;
  threadschedule::thread_config workers;
  workers.set_scheduling(threadschedule::schedule::nice(threadschedule::nice_value{ 10 }));
  config.set_worker_count(threadschedule::worker_count{ 1 }).set_worker_config(std::move(workers));
  threadschedule::thread_pool pool(config);

  auto observed = pool.submit(
      []
        {
          errno = 0;
          return getpriority(PRIO_PROCESS, 0);
        });
  ASSERT_TRUE(observed.has_value());
  EXPECT_EQ(observed->get(), 10);
}
#endif
