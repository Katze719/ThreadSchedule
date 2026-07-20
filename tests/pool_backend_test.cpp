#include <atomic>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <numeric>
#include <threadschedule/chaos.hpp>
#include <threadschedule/task_group.hpp>
#include <threadschedule/threadschedule.hpp>
#include <vector>

using namespace threadschedule;

// ==================== Backend try_submit / try_post ====================

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
  EXPECT_EQ(result.error(),
            std::make_error_code(std::errc::operation_canceled));
}

TEST(PoolBackendTest, TryPostReturnsExpected)
{
  thread_pool_backend pool(2);
  std::promise<void> done;
  auto finished = done.get_future();
  auto result = pool.try_post([&done] { done.set_value(); });
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(finished.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  pool.shutdown(shutdown_policy_backend::drain);
}

TEST(PoolBackendTest, TryPostAfterShutdownReturnsError)
{
  thread_pool_backend pool(2);
  pool.shutdown();
  auto result = pool.try_post([] {});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(),
            std::make_error_code(std::errc::operation_canceled));
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

  auto result = pool.try_post([payload = std::move(payload), &done]() mutable
                                { done.set_value(*payload); });

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

  auto result = pool.try_post([payload = std::move(payload), &done]() mutable
                                { done.set_value(*payload); });

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
    tasks.push_back([&count]
                      { count.fetch_add(1, std::memory_order_relaxed); });

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
  EXPECT_EQ(result.error(),
            std::make_error_code(std::errc::operation_canceled));
}

TEST(PoolBackendTest, SubmitBatchOnThreadPool)
{
  thread_pool_backend pool(2);
  std::atomic<int> count{ 0 };
  std::vector<std::function<void()>> tasks;
  for (int i = 0; i < 50; ++i)
    tasks.push_back([&count]
                      { count.fetch_add(1, std::memory_order_relaxed); });

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

  pool.parallel_for_each(indices.begin(), indices.end(),
                         [&values](int idx) { values[idx].store(idx * 2); });

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
  EXPECT_EQ(completed.wait_for(std::chrono::seconds(5)),
            std::future_status::ready);
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

  pool.set_on_task_start(
      [&starts](auto, auto)
        { starts.fetch_add(1, std::memory_order_relaxed); });
  pool.set_on_task_end([&ends](auto, auto, auto)
                         { ends.fetch_add(1, std::memory_order_relaxed); });

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

  pool.set_on_task_start(
      [&starts](auto, auto)
        { starts.fetch_add(1, std::memory_order_relaxed); });
  pool.set_on_task_end([&ends](auto, auto, auto)
                         { ends.fetch_add(1, std::memory_order_relaxed); });

  for (int i = 0; i < 10; ++i)
    pool.post([] {});

  pool.wait_for_tasks();
  EXPECT_EQ(starts.load(), 10);
  EXPECT_EQ(ends.load(), 10);
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

TEST(PoolBackendTest, LightweightPoolPostBatch)
{
  lightweight_pool_backend pool(4);
  std::atomic<int> count{ 0 };
  std::vector<std::function<void()>> tasks;
  for (int i = 0; i < 100; ++i)
    tasks.push_back([&count]
                      { count.fetch_add(1, std::memory_order_relaxed); });

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
      EXPECT_EQ(r.error(),
                std::make_error_code(std::errc::operation_not_permitted));
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

  scheduler.schedule_after(std::chrono::milliseconds(50),
                           [&ran] { ran = true; });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(ran);
}

TEST(PoolBackendTest, ScheduledPeriodicRunsMultipleTimes)
{
  scheduled_pool_backend scheduler(2);
  std::atomic<int> count{ 0 };

  auto handle = scheduler.schedule_periodic(
      std::chrono::milliseconds(30),
      [&count] { count.fetch_add(1, std::memory_order_relaxed); });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  handle.cancel();
  scheduler.shutdown();
  EXPECT_GE(count.load(), 3);
}

TEST(PoolBackendTest, ScheduledCancel)
{
  scheduled_pool_backend scheduler(2);
  std::atomic<bool> ran{ false };

  auto handle = scheduler.schedule_after(std::chrono::milliseconds(200),
                                         [&ran] { ran = true; });
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
                           [payload = std::move(payload), &done]() mutable
                             { done.set_value(*payload); });

  EXPECT_EQ(finished.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  EXPECT_EQ(finished.get(), 55);
}

TEST(PoolBackendTest, ScheduledHPPool)
{
  scheduled_work_stealing_pool_backend scheduler(2);
  std::atomic<bool> ran{ false };
  scheduler.schedule_after(std::chrono::milliseconds(20),
                           [&ran] { ran = true; });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(ran);
}

TEST(PoolBackendTest, ScheduledLightweight)
{
  scheduled_lightweight_pool_backend scheduler(2);
  std::atomic<bool> ran{ false };
  scheduler.schedule_after(std::chrono::milliseconds(20),
                           [&ran] { ran = true; });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(ran);
}

TEST(PoolBackendTest, ChaosControllerThreadCanBeConfigured)
{
  chaos_config cfg;
  cfg.interval = std::chrono::milliseconds(10);
  cfg.shuffle_affinity = false;
  cfg.priority_jitter = 0;

  chaos_controller chaos(cfg, [](registered_thread_info_backend const&)
                           { return false; });

  ASSERT_TRUE(chaos.configure_thread("chaos_cfg").has_value());

  auto info = chaos.thread_info();
  ASSERT_TRUE(info.has_value());

  auto const name = info->get_name();
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(name.value(), "chaos_cfg");
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
  auto f
      = pool.submit([]() -> int { throw std::runtime_error("inline boom"); });
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
      group.submit([&count]
                     { count.fetch_add(1, std::memory_order_relaxed); });
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
  group.submit(
      [] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
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
