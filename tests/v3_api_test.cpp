#include <threadschedule/threadschedule.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>

#ifndef _WIN32
#  include <sys/resource.h>
#endif

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

TEST(V3Api, PortablePriorityFactoriesExposeLevelsAndNiceValues)
{
  constexpr auto high = threadschedule::schedule::priority(
      threadschedule::priority_level::high);
  constexpr auto nice = threadschedule::schedule::nice(10);

  static_assert(high.intent == threadschedule::scheduling_intent::nice);
  static_assert(high.priority == -5);
  static_assert(nice.intent == threadschedule::scheduling_intent::nice);
  static_assert(nice.priority == 10);
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

TEST(V3Api, InvalidNiceValueDoesNotRunConfiguredThread)
{
  threadschedule::thread_config config;
  config.scheduling = threadschedule::schedule::nice(20);
  std::atomic<bool> ran{ false };

  auto worker
      = threadschedule::thread::create(config, [&ran] { ran.store(true); });

  ASSERT_FALSE(worker.has_value());
  EXPECT_EQ(worker.error(), std::make_error_code(std::errc::invalid_argument));
  EXPECT_FALSE(ran.load());
}

#ifndef _WIN32
TEST(V3Api, ConfiguredThreadObservesExactLinuxNiceValueBeforeCallable)
{
  threadschedule::thread_config config;
  config.scheduling = threadschedule::schedule::nice(10);
  std::promise<int> observed;

  threadschedule::thread worker(config,
                                [&observed]
                                  {
                                    errno = 0;
                                    observed.set_value(
                                        getpriority(PRIO_PROCESS, 0));
                                  });

  EXPECT_EQ(observed.get_future().get(), 10);
  ASSERT_TRUE(worker.join().has_value());
}

TEST(V3Api, ThreadViewRequiresTidForLinuxNiceControl)
{
  std::promise<threadschedule::native_thread_id> started;
  std::promise<void> release;
  auto ready = release.get_future().share();
  std::thread value(
      [&started, ready]
        {
          started.set_value(threadschedule::thread_info::get_thread_id());
          ready.wait();
        });
  auto const tid = started.get_future().get();

  threadschedule::thread_view unknown(value);
  auto unsupported = unknown.set_nice(10);
  threadschedule::thread_view known(value, static_cast<std::uint64_t>(tid));
  auto set = known.set_nice(10);
  auto priority = known.get_priority();

  release.set_value();
  value.join();

  ASSERT_FALSE(unsupported.has_value());
  EXPECT_EQ(unsupported.error(),
            std::make_error_code(std::errc::operation_not_supported));
  ASSERT_TRUE(set.has_value()) << set.error().message();
  ASSERT_TRUE(priority.has_value()) << priority.error().message();
  EXPECT_EQ(priority.value(), threadschedule::priority_level::lowest);
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
  EXPECT_EQ(detached.error(),
            std::make_error_code(std::errc::invalid_argument));
  EXPECT_THROW(worker.join_or_throw(), std::system_error);
  EXPECT_THROW(worker.detach_or_throw(), std::system_error);
}

#ifndef _WIN32
TEST(V3Api, FailedThreadConfigurationDoesNotRunCallable)
{
  threadschedule::thread_config config;
  config.name = "this-name-is-longer-than-fifteen-characters";
  std::atomic<bool> ran{ false };

  auto worker
      = threadschedule::thread::create(config, [&ran] { ran.store(true); });

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

TEST(V3Api, JThreadConfigurationConstructor)
{
  threadschedule::thread_config config;
  config.name = "v3-jthread";

  threadschedule::jthread worker(config, [](std::stop_token) {});
  ASSERT_TRUE(worker.join().has_value());
}

#  ifndef _WIN32
TEST(V3Api, ConfiguredJThreadObservesExactLinuxNiceValue)
{
  threadschedule::thread_config config;
  config.scheduling = threadschedule::schedule::nice(10);
  std::promise<int> observed;

  threadschedule::jthread worker(config,
                                 [&observed](std::stop_token)
                                   {
                                     errno = 0;
                                     observed.set_value(
                                         getpriority(PRIO_PROCESS, 0));
                                   });

  EXPECT_EQ(observed.get_future().get(), 10);
  ASSERT_TRUE(worker.join().has_value());
}
#  endif

#  ifndef _WIN32
TEST(V3Api, FailedJThreadConfigurationDoesNotRunCallable)
{
  threadschedule::thread_config config;
  config.name = "this-name-is-longer-than-fifteen-characters";
  std::atomic<bool> ran{ false };

  auto worker = threadschedule::jthread::create(config, [&ran](std::stop_token)
                                                  { ran.store(true); });

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

TEST(V3Api, MovedFromPoolsRemainSafe)
{
  auto expect_stopped = [](threadschedule::thread_pool& pool)
    {
      EXPECT_EQ(pool.size(), 0u);

      auto submitted = pool.submit([] { return 1; });
      ASSERT_FALSE(submitted.has_value());
      EXPECT_EQ(submitted.error(),
                std::make_error_code(std::errc::operation_canceled));

      auto posted = pool.post([] {});
      ASSERT_FALSE(posted.has_value());
      EXPECT_EQ(posted.error(),
                std::make_error_code(std::errc::operation_canceled));

      auto waited = pool.wait();
      ASSERT_FALSE(waited.has_value());
      EXPECT_EQ(waited.error(),
                std::make_error_code(std::errc::operation_canceled));

      threadschedule::thread_config config;
      auto configured = pool.configure_workers(config);
      ASSERT_FALSE(configured.has_value());
      EXPECT_EQ(configured.error(),
                std::make_error_code(std::errc::operation_canceled));

      try
        {
          pool.wait_or_throw();
          FAIL() << "wait_or_throw should reject a moved-from pool";
        }
      catch (std::system_error const& error)
        {
          EXPECT_EQ(error.code(),
                    std::make_error_code(std::errc::operation_canceled));
        }

      EXPECT_TRUE(pool.shutdown().has_value());
    };

  threadschedule::thread_pool source(1);
  threadschedule::thread_pool moved(std::move(source));
  expect_stopped(source);

  auto first = moved.submit([] { return 41; });
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->get(), 41);

  threadschedule::thread_pool assigned(1);
  assigned = std::move(moved);
  expect_stopped(moved);

  auto second = assigned.submit([] { return 42; });
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->get(), 42);
}

TEST(V3Api, PoolMoveAssignmentUsesDestinationShutdownPolicy)
{
  threadschedule::thread_pool_config config;
  config.worker_count = 1;
  config.shutdown = threadschedule::shutdown_policy::drop_pending;
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

  threadschedule::thread_pool source(1);
  auto assignment = std::async(std::launch::async, [&destination, &source]
                                 { destination = std::move(source); });

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
  pool_config.worker_count = 12;
  pool_config.workers.name = "123456789012345";
  auto pool = threadschedule::thread_pool::create(std::move(pool_config));
  ASSERT_TRUE(pool.has_value()) << pool.error().message();

  threadschedule::scheduled_pool_config scheduled_config;
  scheduled_config.worker_count = 12;
  scheduled_config.workers.name = "1234567890123";
  auto scheduler
      = threadschedule::scheduled_pool::create(std::move(scheduled_config));
  ASSERT_TRUE(scheduler.has_value()) << scheduler.error().message();
}
#endif

TEST(V3Api, ScheduledPoolReportsShutdown)
{
  threadschedule::scheduled_pool scheduler(1);
  ASSERT_TRUE(scheduler.shutdown().has_value());

  auto scheduled = scheduler.schedule_after(1ms, [] {});
  ASSERT_FALSE(scheduled.has_value());
  EXPECT_EQ(scheduled.error(),
            std::make_error_code(std::errc::operation_canceled));
}

TEST(V3Api, ScheduledPoolValidatesPeriodicIntervals)
{
  threadschedule::scheduled_pool scheduler(1);

  auto zero = scheduler.schedule_periodic(0ms, [] {});
  ASSERT_FALSE(zero.has_value());
  EXPECT_EQ(zero.error(), std::make_error_code(std::errc::invalid_argument));

  auto negative = scheduler.schedule_periodic_after(1ms, -1ms, [] {});
  ASSERT_FALSE(negative.has_value());
  EXPECT_EQ(negative.error(),
            std::make_error_code(std::errc::invalid_argument));
}

TEST(V3Api, ScheduledPoolSupportsDelayedPeriodicTasks)
{
  threadschedule::scheduled_pool scheduler(1);
  std::promise<void> first_run;
  auto ready = first_run.get_future();
  std::atomic<bool> reported{ false };

  auto scheduled
      = scheduler.schedule_periodic_after(40ms, 20ms,
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
  threadschedule::scheduled_pool scheduler(1);
  std::promise<int> immediate_run;
  auto immediate_ready = immediate_run.get_future();
  std::promise<int> delayed_run;
  auto delayed_ready = delayed_run.get_future();

  auto immediate = scheduler.schedule_periodic(
      20ms,
      [payload = std::make_unique<int>(41), &immediate_run,
       reported = false]() mutable
        {
          if (!reported)
            {
              reported = true;
              immediate_run.set_value(*payload);
            }
        });
  auto delayed = scheduler.schedule_periodic_after(
      10ms, 20ms,
      [payload = std::make_unique<int>(42), &delayed_run,
       reported = false]() mutable
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
  config.worker_count = 1;
  config.workers.name = "v3-worker";
  config.scheduler.name = "v3-scheduler";
  config.on_task_error = [&reported](threadschedule::task_error const& error)
    { reported.set_value(error.what()); };

  threadschedule::scheduled_pool scheduler(std::move(config));
  auto scheduled = scheduler.schedule_after(
      0ms, [] { throw std::runtime_error("scheduled boom"); });

  ASSERT_TRUE(scheduled.has_value());
  EXPECT_EQ(report.get(), "scheduled boom");
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

TEST(V3Api, AffinityRejectsPartiallyRepresentableMasks)
{
#ifdef _WIN32
  constexpr int unsupported_cpu = 64;
#else
  constexpr int unsupported_cpu = CPU_SETSIZE;
#endif
  threadschedule::thread_affinity affinity({ 0, unsupported_cpu });
  std::promise<void> release;
  auto ready = release.get_future().share();
  threadschedule::thread worker([ready] { ready.wait(); });

  auto configured = worker.set_affinity(affinity);

  release.set_value();
  ASSERT_TRUE(worker.join().has_value());
  ASSERT_FALSE(configured.has_value());
  EXPECT_EQ(configured.error(),
            std::make_error_code(std::errc::invalid_argument));
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
  config.name = "v3-control";
  EXPECT_TRUE(
      registry.configure(snapshot->front().native_id, config).has_value());

  EXPECT_TRUE(registry.unregister_current_thread().has_value());
  EXPECT_TRUE(registry.empty());
}

TEST(V3Api, MovedFromRegistriesRemainSafe)
{
  threadschedule::thread_registry source;
  ASSERT_TRUE(
      source.register_current_thread("original", "source").has_value());

  threadschedule::thread_registry moved(std::move(source));
  // Intentionally verify the documented moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_TRUE(source.empty());
  EXPECT_EQ(source.count(), 0u);
  auto source_snapshot = source.snapshot();
  ASSERT_TRUE(source_snapshot.has_value());
  EXPECT_TRUE(source_snapshot->empty());

  auto source_registration
      = source.register_current_thread("reused", "source");
  ASSERT_FALSE(source_registration.has_value());
  EXPECT_EQ(source_registration.error(),
            std::make_error_code(std::errc::operation_canceled));

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
  EXPECT_EQ(moved_registration.error(),
            std::make_error_code(std::errc::operation_canceled));

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
  ASSERT_TRUE(
      replacement.register_current_thread("replacement", "v3").has_value());

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

TEST(V3Api, RegistrySetsAndReadsPortablePriority)
{
  threadschedule::thread_registry registry;
  std::promise<bool> registered;
  std::promise<void> release;
  auto ready = release.get_future().share();
  threadschedule::thread worker(
      [&registry, &registered, ready]
        {
          registered.set_value(
              registry.register_current_thread("priority", "v3").has_value());
          ready.wait();
          (void)registry.unregister_current_thread();
        });

  bool const registered_ok = registered.get_future().get();
  auto snapshot = registry.snapshot();
  threadschedule::result<void> set = threadschedule::unexpected(
      std::make_error_code(std::errc::no_such_process));
  threadschedule::result<threadschedule::priority_level> priority
      = threadschedule::unexpected(
          std::make_error_code(std::errc::no_such_process));
  if (registered_ok && snapshot.has_value() && snapshot->size() == 1)
    {
      auto const native_id = snapshot->front().native_id;
      set = registry.set_nice(native_id, 10);
      priority = registry.get_priority(native_id);
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
  config.worker_count = 1;
  config.workers.scheduling = threadschedule::schedule::nice(10);
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
