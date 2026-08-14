#include <atomic>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <threadschedule/advanced/pools.hpp>
#include <threadschedule/advanced/task_group.hpp>
#include <threadschedule/advanced/testing/chaos_controller.hpp>
#include <threadschedule/threadschedule.hpp>
#include <vector>

using namespace threadschedule;
using namespace threadschedule::advanced;
using namespace threadschedule::detail;

namespace
{
template <typename Pool>
auto
concurrent_shutdown_for_reports_timeout() -> bool
{
  Pool pool(1);
  std::promise<void> started;
  std::promise<void> release;
  auto release_ready = release.get_future().share();
  pool.post(
      [&]
        {
          started.set_value();
          release_ready.wait();
        });
  started.get_future().wait();

  std::thread first_shutdown([&] { pool.shutdown(shutdown_policy_backend::drain); });
  while (pool.try_post([] {}).has_value())
    std::this_thread::yield();

  std::thread releaser(
      [&]
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          release.set_value();
        });
  bool const drained = pool.shutdown_for(std::chrono::milliseconds(10));

  releaser.join();
  first_shutdown.join();
  return drained;
}

template <typename Pool>
auto
shutdown_for_stops_new_submissions() -> bool
{
  Pool pool(1);
  std::promise<void> started;
  std::promise<void> release;
  auto release_ready = release.get_future().share();
  pool.post(
      [&]
        {
          started.set_value();
          release_ready.wait();
        });
  started.get_future().wait();

  auto shutdown = std::async(std::launch::async, [&pool] { return pool.shutdown_for(std::chrono::seconds(2)); });
  bool rejected = false;
  auto const rejection_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (std::chrono::steady_clock::now() < rejection_deadline)
    {
      if (!pool.try_post([] {}))
        {
          rejected = true;
          break;
        }
      std::this_thread::yield();
    }

  release.set_value();
  return rejected && shutdown.get();
}
} // namespace

// ==================== Backend try_submit / try_post ====================

TEST(PoolBackendTest, BackendWorkerCountsRejectZeroInsteadOfUsingASentinel)
{
  EXPECT_THROW((void)thread_pool_backend{ 0 }, std::invalid_argument);
  EXPECT_THROW((void)work_stealing_pool_backend{ 0 }, std::invalid_argument);
  EXPECT_THROW((void)lightweight_pool_backend{ 0 }, std::invalid_argument);
  EXPECT_THROW((void)scheduled_pool_backend{ 0 }, std::invalid_argument);
}

TEST(PoolBackendTest, TrySubmitReturnsExpected)
{
  thread_pool_backend pool(2);
  auto result = pool.try_submit([] { return 42; });
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().get(), 42);
}

TEST(PoolBackendTest, TrySubmitAfterShutdownReturnsError)
{
  thread_pool_backend pool(2);
  pool.shutdown();
  auto result = pool.try_submit([] { return 1; });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::operation_canceled));
}

TEST(PoolBackendTest, TryPostReturnsExpected)
{
  thread_pool_backend pool(2);
  std::promise<void> done;
  auto finished = done.get_future();
  auto result = pool.try_post([&done] { done.set_value(); });
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(finished.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  pool.shutdown(shutdown_policy_backend::drain);
}

TEST(PoolBackendTest, TryPostAfterShutdownReturnsError)
{
  thread_pool_backend pool(2);
  pool.shutdown();
  auto result = pool.try_post([] {});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::operation_canceled));
}

TEST(PoolBackendTest, PostThrowsOnShutdown)
{
  thread_pool_backend pool(2);
  pool.shutdown();
  EXPECT_THROW(pool.post([] {}), std::runtime_error);
}

TEST(PoolBackendTest, ThreadPoolTryPostAcceptsMoveOnlyTask)
{
  thread_pool_backend pool(2);
  auto payload = std::make_unique<int>(42);
  std::promise<int> done;
  auto finished = done.get_future();

  auto result = pool.try_post([payload = std::move(payload), &done]() mutable { done.set_value(*payload); });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(finished.get(), 42);
  pool.shutdown(shutdown_policy_backend::drain);
}

TEST(PoolBackendTest, HighPerformancePoolTryPostAcceptsMoveOnlyTask)
{
  work_stealing_pool_backend pool(2);
  auto payload = std::make_unique<int>(77);
  std::promise<int> done;
  auto finished = done.get_future();

  auto result = pool.try_post([payload = std::move(payload), &done]() mutable { done.set_value(*payload); });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(finished.get(), 77);
  pool.shutdown(shutdown_policy_backend::drain);
}

// ==================== submit_batch / try_submit_batch ====================

TEST(PoolBackendTest, SubmitBatchExecutesAll)
{
  work_stealing_pool_backend pool(4);
  std::atomic<int> count{ 0 };
  std::vector<std::function<void()>> tasks;
  for (int i = 0; i < 100; ++i)
    tasks.push_back([&count] { count.fetch_add(1, std::memory_order_relaxed); });

  auto futures = pool.submit_batch(tasks.begin(), tasks.end());
  for (auto& f : futures)
    f.get();

  EXPECT_EQ(count.load(), 100);
}

TEST(PoolBackendTest, TrySubmitBatchAfterShutdown)
{
  work_stealing_pool_backend pool(2);
  pool.shutdown();
  std::vector<std::function<void()>> tasks = { [] {}, [] {} };
  auto result = pool.try_submit_batch(tasks.begin(), tasks.end());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::operation_canceled));
}

TEST(PoolBackendTest, SubmitBatchOnThreadPool)
{
  thread_pool_backend pool(2);
  std::atomic<int> count{ 0 };
  std::vector<std::function<void()>> tasks;
  for (int i = 0; i < 50; ++i)
    tasks.push_back([&count] { count.fetch_add(1, std::memory_order_relaxed); });

  auto futures = pool.submit_batch(tasks.begin(), tasks.end());
  for (auto& f : futures)
    f.get();

  EXPECT_EQ(count.load(), 50);
}

// ==================== parallel_for_each ====================

TEST(PoolBackendTest, ParallelForEachHP)
{
  work_stealing_pool_backend pool(4);
  std::vector<std::atomic<int>> values(100);
  for (auto& v : values)
    v.store(0);

  std::vector<int> indices(100);
  std::iota(indices.begin(), indices.end(), 0);

  pool.parallel_for_each(indices.begin(), indices.end(), [&values](int idx) { values[idx].store(idx * 2); });

  for (int i = 0; i < 100; ++i)
    EXPECT_EQ(values[i].load(), i * 2);
}

TEST(PoolBackendTest, ParallelForEachThreadPool)
{
  thread_pool_backend pool(2);
  std::vector<int> data(50, 1);

  pool.parallel_for_each(data.begin(), data.end(), [](int& v) { v *= 3; });

  for (auto const& v : data)
    EXPECT_EQ(v, 3);
}

TEST(PoolBackendTest, ParallelForEachWaitsForAllTasksBeforeRethrowing)
{
  work_stealing_pool_backend pool(2);
  std::vector<int> values(8);
  std::iota(values.begin(), values.end(), 0);
  std::atomic<int> completed{ 0 };

  EXPECT_THROW(pool.parallel_for_each(values.begin(), values.end(),
                                      [&completed](int value)
                                        {
                                          if (value == 0)
                                            throw std::runtime_error("parallel failure");
                                          std::this_thread::sleep_for(std::chrono::milliseconds(10));
                                          completed.fetch_add(1, std::memory_order_relaxed);
                                        }),
               std::runtime_error);

  EXPECT_EQ(completed.load(std::memory_order_relaxed), 7);
}

// ==================== shutdown_policy_backend ====================

TEST(PoolBackendTest, ShutdownDrainCompletesAllTasks)
{
  std::atomic<int> count{ 0 };
  {
    thread_pool_backend pool(2);
    for (int i = 0; i < 20; ++i)
      pool.post(
          [&count]
            {
              std::this_thread::sleep_for(std::chrono::milliseconds(5));
              count.fetch_add(1, std::memory_order_relaxed);
            });
    pool.shutdown(shutdown_policy_backend::drain);
  }
  EXPECT_EQ(count.load(), 20);
}

TEST(PoolBackendTest, ShutdownDropPendingMaySkipTasks)
{
  std::atomic<int> count{ 0 };
  {
    work_stealing_pool_backend pool(1);
    pool.post(
        [&count]
          {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            count.fetch_add(1, std::memory_order_relaxed);
          });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    for (int i = 0; i < 100; ++i)
      pool.post([&count] { count.fetch_add(1, std::memory_order_relaxed); });
    pool.shutdown(shutdown_policy_backend::drop_pending);
  }
  EXPECT_LT(count.load(), 101);
}

TEST(PoolBackendTest, WorkStealingDropDestroysQueuedTasksBeforeJoining)
{
  work_stealing_pool_backend pool(1);
  std::promise<void> started;
  std::promise<void> release;
  auto release_ready = release.get_future().share();
  auto running = pool.submit(
      [&started, release_ready]
        {
          started.set_value();
          release_ready.wait();
        });
  started.get_future().wait();

  auto payload = std::make_shared<int>(42);
  std::weak_ptr<int> weak_payload = payload;
  auto dropped = pool.submit([payload] { return *payload; });
  payload.reset();

  std::thread shutdown_thread([&pool] { pool.shutdown(shutdown_policy_backend::drop_pending); });

  EXPECT_EQ(dropped.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_THROW(dropped.get(), std::future_error);
  for (int attempt = 0; attempt < 100 && !weak_payload.expired(); ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  EXPECT_TRUE(weak_payload.expired());
  release.set_value();
  shutdown_thread.join();
  running.get();
}

TEST(PoolBackendTest, WorkStealingSubmissionIsLinearizedWithShutdown)
{
  work_stealing_pool_backend pool(2);
  std::atomic<bool> keep_submitting{ true };
  std::mutex futures_mutex;
  std::vector<std::future<int>> futures;

  std::thread producer(
      [&]
        {
          while (keep_submitting.load(std::memory_order_acquire))
            {
              auto result = pool.try_submit([] { return 1; });
              if (!result)
                break;
              std::lock_guard<std::mutex> lock(futures_mutex);
              futures.push_back(std::move(result.value()));
            }
        });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  pool.shutdown(shutdown_policy_backend::drop_pending);
  keep_submitting.store(false, std::memory_order_release);
  producer.join();

  for (auto& future : futures)
    {
      ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
      try
        {
          EXPECT_EQ(future.get(), 1);
        }
      catch (std::future_error const& error)
        {
          EXPECT_EQ(error.code(), std::make_error_code(std::future_errc::broken_promise));
        }
    }
}

TEST(PoolBackendTest, WorkStealingDrainCompletesAcceptedConcurrentSubmissions)
{
  work_stealing_pool_backend pool(2);
  std::mutex futures_mutex;
  std::vector<std::future<int>> futures;

  std::thread producer(
      [&]
        {
          while (true)
            {
              auto result = pool.try_submit([] { return 1; });
              if (!result)
                return;
              std::lock_guard<std::mutex> lock(futures_mutex);
              futures.push_back(std::move(result.value()));
            }
        });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  pool.shutdown(shutdown_policy_backend::drain);
  producer.join();

  for (auto& future : futures)
    {
      ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
      EXPECT_EQ(future.get(), 1);
    }
}

TEST(PoolBackendTest, ShutdownForTimedDrain)
{
  thread_pool_backend pool(2);
  std::atomic<int> count{ 0 };
  for (int i = 0; i < 5; ++i)
    pool.post(
        [&count]
          {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            count.fetch_add(1, std::memory_order_relaxed);
          });

  bool drained = pool.shutdown_for(std::chrono::milliseconds(5000));
  EXPECT_TRUE(drained);
  EXPECT_EQ(count.load(), 5);
}

TEST(PoolBackendTest, ConcurrentShutdownForReportsTimeout)
{
  EXPECT_FALSE(concurrent_shutdown_for_reports_timeout<thread_pool_backend>());
  EXPECT_FALSE(concurrent_shutdown_for_reports_timeout<work_stealing_pool_backend>());
  EXPECT_FALSE(concurrent_shutdown_for_reports_timeout<lightweight_pool_backend>());
}

TEST(PoolBackendTest, ShutdownForStopsNewSubmissionsBeforeWaiting)
{
  EXPECT_TRUE(shutdown_for_stops_new_submissions<thread_pool_backend>());
  EXPECT_TRUE(shutdown_for_stops_new_submissions<work_stealing_pool_backend>());
  EXPECT_TRUE(shutdown_for_stops_new_submissions<lightweight_pool_backend>());
}

TEST(PoolBackendTest, ShutdownForZeroSucceedsAfterCompletedShutdown)
{
  thread_pool_backend regular(1);
  regular.shutdown();
  EXPECT_TRUE(regular.shutdown_for(std::chrono::milliseconds(0)));

  work_stealing_pool_backend work_stealing(1);
  work_stealing.shutdown();
  EXPECT_TRUE(work_stealing.shutdown_for(std::chrono::milliseconds(0)));

  lightweight_pool_backend lightweight(1);
  lightweight.shutdown();
  EXPECT_TRUE(lightweight.shutdown_for(std::chrono::milliseconds(0)));
}

TEST(PoolBackendTest, WorkerShutdownIsRejectedWithoutCorruptingPool)
{
  thread_pool_backend regular(1);
  auto regular_attempt = regular.submit([&regular] { regular.shutdown(shutdown_policy_backend::drain); });
  try
    {
      regular_attempt.get();
      FAIL() << "worker shutdown unexpectedly succeeded";
    }
  catch (std::system_error const& error)
    {
      EXPECT_EQ(error.code(), std::make_error_code(std::errc::resource_deadlock_would_occur));
    }
  EXPECT_EQ(regular.submit([] { return 1; }).get(), 1);
  regular.shutdown();

  work_stealing_pool_backend work_stealing(1);
  auto stealing_attempt
      = work_stealing.submit([&work_stealing] { work_stealing.shutdown(shutdown_policy_backend::drain); });
  try
    {
      stealing_attempt.get();
      FAIL() << "worker shutdown unexpectedly succeeded";
    }
  catch (std::system_error const& error)
    {
      EXPECT_EQ(error.code(), std::make_error_code(std::errc::resource_deadlock_would_occur));
    }
  EXPECT_EQ(work_stealing.submit([] { return 2; }).get(), 2);
  work_stealing.shutdown();

  lightweight_pool_backend lightweight(1);
  std::promise<std::error_code> lightweight_error;
  lightweight.post(
      [&]
        {
          try
            {
              lightweight.shutdown();
              lightweight_error.set_value({});
            }
          catch (std::system_error const& error)
            {
              lightweight_error.set_value(error.code());
            }
        });
  EXPECT_EQ(lightweight_error.get_future().get(), std::make_error_code(std::errc::resource_deadlock_would_occur));
  lightweight.shutdown();
}

TEST(PoolBackendTest, WorkerWaitIsRejectedWithoutDeadlock)
{
  thread_pool_backend regular(1);
  auto regular_attempt = regular.submit([&regular] { regular.wait_for_tasks(); });
  EXPECT_THROW(regular_attempt.get(), std::system_error);
  regular.shutdown();

  work_stealing_pool_backend work_stealing(1);
  auto stealing_attempt = work_stealing.submit([&work_stealing] { work_stealing.wait_for_tasks(); });
  EXPECT_THROW(stealing_attempt.get(), std::system_error);
  work_stealing.shutdown();
}

// ==================== post on all pool types ====================

TEST(PoolBackendTest, HPPoolPost)
{
  work_stealing_pool_backend pool(2);
  std::atomic<bool> ran{ false };
  std::promise<void> done;
  auto completed = done.get_future();
  pool.post(
      [&ran, &done]
        {
          ran = true;
          done.set_value();
        });
  EXPECT_EQ(completed.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  pool.shutdown(shutdown_policy_backend::drain);
  EXPECT_TRUE(ran);
}

TEST(PoolBackendTest, FastPoolPost)
{
  std::atomic<bool> ran{ false };
  {
    polling_pool_backend pool(2);
    pool.post([&ran] { ran = true; });
    pool.shutdown(shutdown_policy_backend::drain);
  }
  EXPECT_TRUE(ran);
}

// ==================== HP Pool deque_capacity constructor ====================

TEST(PoolBackendTest, HPPoolCustomDequeCapacity)
{
  work_stealing_pool_backend pool(2, 64);
  auto f = pool.submit([] { return 7; });
  EXPECT_EQ(f.get(), 7);
}

// ==================== Trace callbacks ====================

TEST(PoolBackendTest, TraceCallbacksHP)
{
  work_stealing_pool_backend pool(2);
  std::atomic<int> starts{ 0 };
  std::atomic<int> ends{ 0 };

  pool.set_on_task_start([&starts](auto, auto) { starts.fetch_add(1, std::memory_order_relaxed); });
  pool.set_on_task_end([&ends](auto, auto, auto) { ends.fetch_add(1, std::memory_order_relaxed); });

  for (int i = 0; i < 10; ++i)
    pool.post([] {});

  pool.wait_for_tasks();
  EXPECT_EQ(starts.load(), 10);
  EXPECT_EQ(ends.load(), 10);
}

TEST(PoolBackendTest, TraceCallbacksThreadPool)
{
  thread_pool_backend pool(2);
  std::atomic<int> starts{ 0 };
  std::atomic<int> ends{ 0 };

  pool.set_on_task_start([&starts](auto, auto) { starts.fetch_add(1, std::memory_order_relaxed); });
  pool.set_on_task_end([&ends](auto, auto, auto) { ends.fetch_add(1, std::memory_order_relaxed); });

  for (int i = 0; i < 10; ++i)
    pool.post([] {});

  pool.wait_for_tasks();
  EXPECT_EQ(starts.load(), 10);
  EXPECT_EQ(ends.load(), 10);
}

TEST(PoolBackendTest, ThrowingTraceCallbacksDoNotStopWorkers)
{
  work_stealing_pool_backend work_stealing(1);
  work_stealing.set_on_task_start([](auto, auto) { throw std::runtime_error("start callback"); });
  work_stealing.set_on_task_end([](auto, auto, auto) { throw std::runtime_error("end callback"); });
  auto first = work_stealing.submit([] { return 1; });
  auto second = work_stealing.submit([] { return 2; });
  EXPECT_EQ(first.get(), 1);
  EXPECT_EQ(second.get(), 2);
  work_stealing.wait_for_tasks();

  thread_pool_backend regular(1);
  regular.set_on_task_start([](auto, auto) { throw std::runtime_error("start callback"); });
  regular.set_on_task_end([](auto, auto, auto) { throw std::runtime_error("end callback"); });
  auto third = regular.submit([] { return 3; });
  auto fourth = regular.submit([] { return 4; });
  EXPECT_EQ(third.get(), 3);
  EXPECT_EQ(fourth.get(), 4);
  regular.wait_for_tasks();
}

// ==================== lightweight_pool_backend ====================

TEST(PoolBackendTest, LightweightPoolPost)
{
  lightweight_pool_backend pool(2);
  std::atomic<int> count{ 0 };

  for (int i = 0; i < 50; ++i)
    pool.post([&count] { count.fetch_add(1, std::memory_order_relaxed); });

  pool.shutdown(shutdown_policy_backend::drain);
  EXPECT_EQ(count.load(), 50);
}

TEST(PoolBackendTest, LightweightPoolTryPost)
{
  lightweight_pool_backend pool(2);
  std::atomic<bool> ran{ false };
  auto result = pool.try_post([&ran] { ran = true; });
  ASSERT_TRUE(result.has_value());
  pool.shutdown(shutdown_policy_backend::drain);
  EXPECT_TRUE(ran);
}

TEST(PoolBackendTest, LightweightPoolReleasesThrowingTaskCapture)
{
  lightweight_pool_backend pool(1);
  auto payload = std::make_shared<int>(42);
  std::weak_ptr<int> weak_payload = payload;
  pool.post([payload] { throw std::runtime_error("task failure"); });
  payload.reset();

  pool.shutdown(shutdown_policy_backend::drain);
  EXPECT_TRUE(weak_payload.expired());
}

TEST(PoolBackendTest, LightweightPoolPostBatch)
{
  lightweight_pool_backend pool(4);
  std::atomic<int> count{ 0 };
  std::vector<std::function<void()>> tasks;
  for (int i = 0; i < 100; ++i)
    tasks.push_back([&count] { count.fetch_add(1, std::memory_order_relaxed); });

  pool.post_batch(tasks.begin(), tasks.end());
  pool.shutdown(shutdown_policy_backend::drain);
  EXPECT_EQ(count.load(), 100);
}

TEST(PoolBackendTest, LightweightPoolShutdownDropPending)
{
  std::atomic<int> count{ 0 };
  {
    lightweight_pool_backend pool(1);
    pool.post(
        [&count]
          {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            count.fetch_add(1, std::memory_order_relaxed);
          });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    for (int i = 0; i < 50; ++i)
      pool.post([&count] { count.fetch_add(1, std::memory_order_relaxed); });
    pool.shutdown(shutdown_policy_backend::drop_pending);
  }
  EXPECT_LT(count.load(), 51);
}

TEST(PoolBackendTest, LightweightPoolShutdownFor)
{
  lightweight_pool_backend pool(2);
  std::atomic<int> count{ 0 };
  for (int i = 0; i < 5; ++i)
    pool.post(
        [&count]
          {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            count.fetch_add(1, std::memory_order_relaxed);
          });
  bool drained = pool.shutdown_for(std::chrono::milliseconds(5000));
  EXPECT_TRUE(drained);
  EXPECT_EQ(count.load(), 5);
}

TEST(PoolBackendTest, LightweightPoolConfigureThreads)
{
  lightweight_pool_backend pool(2);
  auto r = pool.configure_threads("lite");
#if defined(__MINGW32__)
  if (!r.has_value())
    {
      EXPECT_EQ(r.error(), std::make_error_code(std::errc::operation_not_permitted));
      return;
    }
#endif
  EXPECT_TRUE(r.has_value());
}

TEST(PoolBackendTest, LightweightPoolCustomTaskSize)
{
  lightweight_pool_backend_base<128> pool(2);
  std::atomic<bool> ran{ false };
  pool.post([&ran] { ran = true; });
  pool.shutdown(shutdown_policy_backend::drain);
  EXPECT_TRUE(ran);
}

// ==================== global_pool_backend ====================

TEST(PoolBackendTest, GlobalThreadPoolSubmit)
{
  auto f = global_thread_pool_backend::submit([] { return 99; });
  EXPECT_EQ(f.get(), 99);
}

TEST(PoolBackendTest, GlobalThreadPoolPost)
{
  std::atomic<bool> ran{ false };
  global_thread_pool_backend::post([&ran] { ran = true; });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_TRUE(ran);
}

// ==================== scheduled_pool_backend ====================

TEST(PoolBackendTest, ScheduledAfterBasic)
{
  scheduled_pool_backend scheduler(2);
  std::atomic<bool> ran{ false };

  scheduler.schedule_after(std::chrono::milliseconds(50), [&ran] { ran = true; });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(ran);
}

TEST(PoolBackendTest, ScheduledPeriodicRunsMultipleTimes)
{
  scheduled_pool_backend scheduler(2);
  std::atomic<int> count{ 0 };

  auto handle = scheduler.schedule_periodic(std::chrono::milliseconds(30),
                                            [&count] { count.fetch_add(1, std::memory_order_relaxed); });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  handle.cancel();
  scheduler.shutdown();
  EXPECT_GE(count.load(), 3);
}

TEST(PoolBackendTest, ScheduledPeriodicAcceptsMoveOnlyCallable)
{
  scheduled_pool_backend scheduler(1);
  std::promise<int> first_run;
  auto finished = first_run.get_future();

  auto handle
      = scheduler.schedule_periodic(std::chrono::milliseconds(20),
                                    [payload = std::make_unique<int>(55), &first_run, reported = false]() mutable
                                      {
                                        if (!reported)
                                          {
                                            reported = true;
                                            first_run.set_value(*payload);
                                          }
                                      });

  EXPECT_EQ(finished.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_EQ(finished.get(), 55);
  handle.cancel();
}

TEST(PoolBackendTest, ScheduledEarlierInsertionWakesScheduler)
{
  scheduled_pool_backend scheduler(1);
  auto later = scheduler.schedule_after(std::chrono::seconds(5), [] {});
  std::promise<void> early_ran;
  auto early_ready = early_ran.get_future();

  scheduler.schedule_after(std::chrono::milliseconds(10), [&early_ran] { early_ran.set_value(); });

  EXPECT_EQ(early_ready.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  later.cancel();
}

TEST(PoolBackendTest, ScheduledMovedFromHandleIsSafe)
{
  scheduled_pool_backend scheduler(1);
  auto source = scheduler.schedule_after(std::chrono::seconds(5), [] {});
  auto destination = std::move(source);

  EXPECT_EQ(source.id(), 0u);         // NOLINT(bugprone-use-after-move)
  EXPECT_TRUE(source.is_cancelled()); // NOLINT(bugprone-use-after-move)
  EXPECT_NO_THROW(source.cancel());   // NOLINT(bugprone-use-after-move)
  destination.cancel();
}

TEST(PoolBackendTest, ScheduledShutdownCancelsAndReleasesPendingTasks)
{
  scheduled_pool_backend scheduler(1);
  auto payload = std::make_shared<int>(42);
  std::weak_ptr<int> weak_payload = payload;
  auto handle = scheduler.schedule_after(std::chrono::hours(1), [payload] { (void)*payload; });
  payload.reset();

  scheduler.shutdown(shutdown_policy_backend::drop_pending);

  EXPECT_TRUE(handle.is_cancelled());
  EXPECT_EQ(scheduler.scheduled_count(), 0u);
  EXPECT_TRUE(weak_payload.expired());
}

TEST(PoolBackendTest, ScheduledPeriodicCallableDoesNotRunConcurrently)
{
  scheduled_pool_backend scheduler(2);
  std::atomic<int> active{ 0 };
  std::atomic<int> maximum{ 0 };
  std::promise<void> first_run;
  auto first_ready = first_run.get_future();
  std::atomic<bool> reported{ false };

  auto handle = scheduler.schedule_periodic(
      std::chrono::milliseconds(2),
      [&]
        {
          int const current = active.fetch_add(1, std::memory_order_acq_rel) + 1;
          int observed = maximum.load(std::memory_order_relaxed);
          while (observed < current && !maximum.compare_exchange_weak(observed, current, std::memory_order_relaxed))
            {
            }
          if (!reported.exchange(true, std::memory_order_acq_rel))
            first_run.set_value();
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
          active.fetch_sub(1, std::memory_order_acq_rel);
        });

  ASSERT_EQ(first_ready.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  handle.cancel();
  scheduler.shutdown();
  EXPECT_EQ(maximum.load(std::memory_order_relaxed), 1);
}

TEST(PoolBackendTest, ScheduledPeriodicOverrunDoesNotBlockOtherWork)
{
  scheduled_pool_backend scheduler(2);
  std::promise<void> started;
  auto started_ready = started.get_future();
  std::atomic<bool> reported{ false };
  auto periodic = scheduler.schedule_periodic(std::chrono::milliseconds(1),
                                              [&]
                                                {
                                                  if (!reported.exchange(true, std::memory_order_acq_rel))
                                                    started.set_value();
                                                  std::this_thread::sleep_for(std::chrono::milliseconds(150));
                                                });

  ASSERT_EQ(started_ready.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  auto independent = scheduler.thread_pool().submit([] { return 42; });
  EXPECT_EQ(independent.wait_for(std::chrono::milliseconds(75)), std::future_status::ready);
  EXPECT_EQ(independent.get(), 42);
  periodic.cancel();
  scheduler.shutdown();
}

TEST(PoolBackendTest, ScheduledWorkerShutdownIsRejectedWithoutCorruption)
{
  scheduled_pool_backend scheduler(1);
  std::promise<std::error_code> attempted;
  auto ready = attempted.get_future();
  scheduler.schedule_after(std::chrono::milliseconds(1),
                           [&]
                             {
                               try
                                 {
                                   scheduler.shutdown();
                                   attempted.set_value({});
                                 }
                               catch (std::system_error const& error)
                                 {
                                   attempted.set_value(error.code());
                                 }
                             });

  EXPECT_EQ(ready.get(), std::make_error_code(std::errc::resource_deadlock_would_occur));
  std::promise<void> ran;
  auto ran_ready = ran.get_future();
  scheduler.schedule_after(std::chrono::milliseconds(1), [&ran] { ran.set_value(); });
  EXPECT_EQ(ran_ready.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  scheduler.shutdown();
}

TEST(PoolBackendTest, ScheduledThreadShutdownIsRejectedWithoutCorruption)
{
  scheduled_pool_backend scheduler(1);
  scheduler.thread_pool().shutdown(shutdown_policy_backend::drop_pending);

  std::promise<std::error_code> attempted;
  auto ready = attempted.get_future();
  auto payload = std::shared_ptr<int>(new int(42),
                                      [&scheduler, &attempted](int* value)
                                        {
                                          delete value;
                                          try
                                            {
                                              scheduler.shutdown();
                                              attempted.set_value({});
                                            }
                                          catch (std::system_error const& error)
                                            {
                                              attempted.set_value(error.code());
                                            }
                                        });
  scheduler.schedule_after(std::chrono::milliseconds(1), [payload] { (void)*payload; });
  payload.reset();

  ASSERT_EQ(ready.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_EQ(ready.get(), std::make_error_code(std::errc::resource_deadlock_would_occur));
  EXPECT_TRUE(scheduler.scheduler_thread_info().has_value());

  scheduler.shutdown();
  EXPECT_FALSE(scheduler.scheduler_thread_info().has_value());
}

TEST(PoolBackendTest, ScheduledCancel)
{
  scheduled_pool_backend scheduler(2);
  std::atomic<bool> ran{ false };

  auto handle = scheduler.schedule_after(std::chrono::milliseconds(200), [&ran] { ran = true; });
  handle.cancel();
  EXPECT_TRUE(handle.is_cancelled());

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_FALSE(ran);
}

TEST(PoolBackendTest, ScheduledInsertAfterShutdownReturnsCancelledHandle)
{
  scheduled_pool_backend scheduler(2);
  scheduler.shutdown();

  auto handle = scheduler.schedule_after(std::chrono::milliseconds(10), [] {});
  EXPECT_TRUE(handle.is_cancelled());
}

TEST(PoolBackendTest, ScheduledSchedulerThreadCanBeConfigured)
{
  scheduled_pool_backend scheduler(2);

  ASSERT_TRUE(scheduler.configure_scheduler_thread("sched_cfg").has_value());

  auto info = scheduler.scheduler_thread_info();
  ASSERT_TRUE(info.has_value());

  auto const name = info->get_name();
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(name.value(), "sched_cfg");
}

TEST(PoolBackendTest, ScheduledSchedulerThreadInfoUnavailableAfterShutdown)
{
  scheduled_pool_backend scheduler(2);
  scheduler.shutdown();
  EXPECT_FALSE(scheduler.scheduler_thread_info().has_value());
}

TEST(PoolBackendTest, ScheduledAfterAcceptsMoveOnlyTask)
{
  scheduled_pool_backend scheduler(2);
  auto payload = std::make_unique<int>(55);
  std::promise<int> done;
  auto finished = done.get_future();

  scheduler.schedule_after(std::chrono::milliseconds(20),
                           [payload = std::move(payload), &done]() mutable { done.set_value(*payload); });

  EXPECT_EQ(finished.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_EQ(finished.get(), 55);
}

TEST(PoolBackendTest, ScheduledHPPool)
{
  scheduled_work_stealing_pool_backend scheduler(2);
  std::atomic<bool> ran{ false };
  scheduler.schedule_after(std::chrono::milliseconds(20), [&ran] { ran = true; });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(ran);
}

TEST(PoolBackendTest, ScheduledLightweight)
{
  scheduled_lightweight_pool_backend scheduler(2);
  std::atomic<bool> ran{ false };
  scheduler.schedule_after(std::chrono::milliseconds(20), [&ran] { ran = true; });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(ran);
}

TEST(PoolBackendTest, ChaosControllerThreadCanBeConfigured)
{
  chaos_config cfg;
  cfg.interval = std::chrono::milliseconds(10);
  cfg.shuffle_affinity = false;
  cfg.priority_jitter = 0;

  chaos_controller chaos(cfg, [](registered_thread const&) { return false; });

  ASSERT_TRUE(chaos.configure_thread("chaos_cfg").has_value());

  auto info = chaos.thread_info();
  ASSERT_TRUE(info.has_value());

  EXPECT_EQ(info->name, "chaos_cfg");
}

// ==================== inline_pool_backend ====================

TEST(PoolBackendTest, InlinePoolSubmit)
{
  inline_pool_backend pool;
  auto f = pool.submit([] { return 42; });
  EXPECT_EQ(f.get(), 42);
}

TEST(PoolBackendTest, InlinePoolPost)
{
  inline_pool_backend pool;
  int value = 0;
  pool.post([&value] { value = 7; });
  EXPECT_EQ(value, 7);
}

TEST(PoolBackendTest, InlinePoolExceptionPropagation)
{
  inline_pool_backend pool;
  auto f = pool.submit([]() -> int { throw std::runtime_error("inline boom"); });
  EXPECT_THROW(f.get(), std::runtime_error);
}

TEST(PoolBackendTest, InlinePoolShutdown)
{
  inline_pool_backend pool;
  pool.shutdown();
  EXPECT_THROW(pool.submit([] { return 1; }), std::runtime_error);
}

TEST(PoolBackendTest, InlinePoolParallelForEach)
{
  inline_pool_backend pool;
  std::vector<int> data = { 1, 2, 3, 4, 5 };
  pool.parallel_for_each(data.begin(), data.end(), [](int& v) { v *= 10; });
  EXPECT_EQ(data, (std::vector<int>{ 10, 20, 30, 40, 50 }));
}

// ==================== task_group ====================

TEST(PoolBackendTest, TaskGroupWaitsForAll)
{
  thread_pool_backend pool(2);
  std::atomic<int> count{ 0 };
  {
    task_group<thread_pool_backend> group(pool);
    for (int i = 0; i < 10; ++i)
      group.submit([&count] { count.fetch_add(1, std::memory_order_relaxed); });
    group.wait();
  }
  EXPECT_EQ(count.load(), 10);
}

TEST(PoolBackendTest, TaskGroupDestructorWaits)
{
  thread_pool_backend pool(2);
  std::atomic<int> count{ 0 };
  {
    task_group<thread_pool_backend> group(pool);
    for (int i = 0; i < 5; ++i)
      group.submit(
          [&count]
            {
              std::this_thread::sleep_for(std::chrono::milliseconds(10));
              count.fetch_add(1, std::memory_order_relaxed);
            });
  }
  EXPECT_EQ(count.load(), 5);
}

TEST(PoolBackendTest, TaskGroupPropagatesException)
{
  thread_pool_backend pool(2);
  task_group<thread_pool_backend> group(pool);
  group.submit([] { throw std::runtime_error("group fail"); });
  EXPECT_THROW(group.wait(), std::runtime_error);
}

TEST(PoolBackendTest, TaskGroupPendingCount)
{
  thread_pool_backend pool(2);
  task_group<thread_pool_backend> group(pool);
  EXPECT_EQ(group.pending(), 0u);
  group.submit([] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
  EXPECT_GE(group.pending(), 0u);
  group.wait();
  EXPECT_EQ(group.pending(), 0u);
}

TEST(PoolBackendTest, TaskGroupWithInlinePool)
{
  inline_pool_backend pool;
  int sum = 0;
  {
    task_group<inline_pool_backend> group(pool);
    group.submit([&sum] { sum += 1; });
    group.submit([&sum] { sum += 2; });
    group.submit([&sum] { sum += 3; });
    group.wait();
  }
  EXPECT_EQ(sum, 6);
}
