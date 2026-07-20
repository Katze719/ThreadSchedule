#include <algorithm>
#include <future>
#include <gtest/gtest.h>
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

class ThreadConfigTest : public ::testing::Test
{
protected:
  void
  SetUp() override
  {
  }

  void
  TearDown() override
  {
  }
};

// ==================== native_thread_priority Tests ====================

TEST_F(ThreadConfigTest, ThreadPriorityDefaultConstruction)
{
  native_thread_priority priority;
  EXPECT_EQ(priority.value(), 0);
}

TEST_F(ThreadConfigTest, ThreadPriorityValueConstruction)
{
  native_thread_priority priority(10);
  EXPECT_EQ(priority.value(), 10);

  native_thread_priority realtime_priority(99);
  EXPECT_EQ(realtime_priority.value(), 99);
}

TEST_F(ThreadConfigTest, ThreadPriorityClampsToSupportedRange)
{
  EXPECT_EQ(native_thread_priority{ -100 }.value(),
            native_thread_priority::highest().value());
  EXPECT_EQ(native_thread_priority{ 150 }.value(),
            native_thread_priority::realtime_highest().value());
}

TEST_F(ThreadConfigTest, ThreadPriorityFactoryMethods)
{
  auto lowest = native_thread_priority::lowest();
  auto normal = native_thread_priority::normal();
  auto highest = native_thread_priority::highest();

  EXPECT_GT(lowest.value(), normal.value());
  EXPECT_EQ(normal.value(), 0);
  EXPECT_GT(normal.value(), highest.value());
  EXPECT_EQ(native_thread_priority::realtime_lowest().value(), 1);
  EXPECT_EQ(native_thread_priority::realtime_highest().value(), 99);
}

TEST_F(ThreadConfigTest, ThreadPriorityComparison)
{
  native_thread_priority p1(5);
  native_thread_priority p2(10);
  native_thread_priority p3(5);

  EXPECT_TRUE(p1 == p3);
  EXPECT_TRUE(p1 != p2);
  EXPECT_TRUE(p1 < p2);
  EXPECT_TRUE(p2 > p1);
  EXPECT_TRUE(p1 <= p2);
  EXPECT_TRUE(p1 <= p3);
  EXPECT_TRUE(p2 >= p1);
  EXPECT_TRUE(p1 >= p3);
}

TEST_F(ThreadConfigTest, ThreadPriorityToString)
{
  native_thread_priority normal = native_thread_priority::normal();
  std::string str = normal.to_string();

  EXPECT_FALSE(str.empty());
  EXPECT_NE(str.find("0"), std::string::npos);
}

TEST_F(ThreadConfigTest, ThreadPriorityMinMax)
{
  auto lowest = native_thread_priority::lowest();
  auto highest = native_thread_priority::highest();

  EXPECT_GT(lowest.value(), highest.value());
}

#ifdef _WIN32
TEST_F(ThreadConfigTest, WindowsPriorityMappingUsesNiceSemantics)
{
  EXPECT_EQ(detail::map_priority_to_win32(-20), THREAD_PRIORITY_HIGHEST);
  EXPECT_EQ(detail::map_priority_to_win32(-10), THREAD_PRIORITY_HIGHEST);
  EXPECT_EQ(detail::map_priority_to_win32(-9), THREAD_PRIORITY_ABOVE_NORMAL);
  EXPECT_EQ(
      detail::map_priority_to_win32(native_thread_priority::highest().value()),
      THREAD_PRIORITY_HIGHEST);
  EXPECT_EQ(
      detail::map_priority_to_win32(native_thread_priority{ -5 }.value()),
      THREAD_PRIORITY_ABOVE_NORMAL);
  EXPECT_EQ(
      detail::map_priority_to_win32(native_thread_priority::normal().value()),
      THREAD_PRIORITY_NORMAL);
  EXPECT_EQ(detail::map_priority_to_win32(1), THREAD_PRIORITY_BELOW_NORMAL);
  EXPECT_EQ(detail::map_priority_to_win32(native_thread_priority{ 5 }.value()),
            THREAD_PRIORITY_BELOW_NORMAL);
  EXPECT_EQ(detail::map_priority_to_win32(9), THREAD_PRIORITY_BELOW_NORMAL);
  EXPECT_EQ(detail::map_priority_to_win32(10), THREAD_PRIORITY_LOWEST);
  EXPECT_EQ(detail::map_priority_to_win32(18), THREAD_PRIORITY_LOWEST);
  EXPECT_EQ(
      detail::map_priority_to_win32(native_thread_priority::lowest().value()),
      THREAD_PRIORITY_IDLE);

  auto params = scheduler_parameters::create_for_policy(
      native_scheduling_policy::other, native_thread_priority::highest());
  ASSERT_TRUE(params.has_value());
  EXPECT_EQ(params.value().sched_priority, THREAD_PRIORITY_HIGHEST);

  auto realtime_params = scheduler_parameters::create_for_policy(
      native_scheduling_policy::fifo,
      native_thread_priority::realtime_highest());
  ASSERT_TRUE(realtime_params.has_value());
  EXPECT_EQ(realtime_params.value().sched_priority,
            THREAD_PRIORITY_TIME_CRITICAL);
}
#endif

// ==================== native_thread_affinity Tests ====================

TEST_F(ThreadConfigTest, ThreadAffinityDefaultConstruction)
{
  native_thread_affinity affinity;
  // Default construction should have some valid state
}

TEST_F(ThreadConfigTest, ThreadAffinityCPUList)
{
  std::vector<int> cpus = { 0, 1, 2 };
  native_thread_affinity affinity(cpus);

  EXPECT_TRUE(affinity.is_set(0));
  EXPECT_TRUE(affinity.is_set(1));
  EXPECT_TRUE(affinity.is_set(2));
  EXPECT_FALSE(affinity.is_set(3));
}

TEST_F(ThreadConfigTest, ThreadAffinityAddRemove)
{
  native_thread_affinity affinity;

  affinity.add_cpu(0);
  EXPECT_TRUE(affinity.is_set(0));

  affinity.remove_cpu(0);
  EXPECT_FALSE(affinity.is_set(0));
}

TEST_F(ThreadConfigTest, ThreadAffinityAddMultiple)
{
  native_thread_affinity affinity;
  affinity.add_cpu(0);
  affinity.add_cpu(1);
  affinity.add_cpu(2);

  // Check added CPUs
  EXPECT_TRUE(affinity.is_set(0));
  EXPECT_TRUE(affinity.is_set(1));
  EXPECT_TRUE(affinity.is_set(2));
}

TEST_F(ThreadConfigTest, ThreadAffinityClear)
{
  native_thread_affinity affinity;
  affinity.add_cpu(0);
  affinity.add_cpu(1);
  affinity.clear();

  // Check that CPUs are cleared
  EXPECT_FALSE(affinity.is_set(0));
  EXPECT_FALSE(affinity.is_set(1));
}

TEST_F(ThreadConfigTest, ThreadAffinityGetCPUs)
{
  native_thread_affinity affinity;
  affinity.add_cpu(0);
  affinity.add_cpu(2);
  affinity.add_cpu(4);

  auto cpus = affinity.get_cpus();
  EXPECT_EQ(cpus.size(), 3);
  EXPECT_TRUE(std::find(cpus.begin(), cpus.end(), 0) != cpus.end());
  EXPECT_TRUE(std::find(cpus.begin(), cpus.end(), 2) != cpus.end());
  EXPECT_TRUE(std::find(cpus.begin(), cpus.end(), 4) != cpus.end());
}

TEST_F(ThreadConfigTest, ThreadAffinityToString)
{
  native_thread_affinity affinity;
  affinity.add_cpu(0);
  affinity.add_cpu(1);

  std::string str = affinity.to_string();
  EXPECT_FALSE(str.empty());
}

#ifndef _WIN32
TEST_F(ThreadConfigTest, ThreadAffinityNativeHandle)
{
  native_thread_affinity affinity;
  affinity.add_cpu(0);

  cpu_set_t const& cpuset = affinity.native_handle();
  EXPECT_TRUE(CPU_ISSET(0, &cpuset));
}
#endif

// ==================== native_scheduling_policy Tests ====================

TEST_F(ThreadConfigTest, SchedulingPolicyValues)
{
  auto other = native_scheduling_policy::other;
  auto fifo = native_scheduling_policy::fifo;
  auto rr = native_scheduling_policy::rr;
  // Just ensure they're different
  EXPECT_NE(static_cast<int>(other), static_cast<int>(fifo));
  EXPECT_NE(static_cast<int>(fifo), static_cast<int>(rr));
}

TEST_F(ThreadConfigTest, SchedulingPolicyToString)
{
  std::vector<native_scheduling_policy> policies
      = { native_scheduling_policy::other, native_scheduling_policy::fifo,
          native_scheduling_policy::rr };
#if defined(SCHED_DEADLINE) && !defined(_WIN32)
  policies.push_back(native_scheduling_policy::deadline);
#endif

  for (auto policy : policies)
    {
      std::string str = to_string(policy);
      EXPECT_FALSE(str.empty());
    }
}

// ==================== scheduler_parameters Tests ====================

TEST_F(ThreadConfigTest, SchedulerParamsCreation)
{
  auto params_result = scheduler_parameters::create_for_policy(
      native_scheduling_policy::other, native_thread_priority::normal());

  // Should succeed for OTHER policy
  EXPECT_TRUE(params_result.has_value());

  if (params_result.has_value())
    {
      auto const& params = params_result.value();
      // Verify it's a valid sched_param
#ifndef _WIN32
      EXPECT_GE(params.sched_priority, 0);
#endif
    }
}

#ifndef _WIN32
TEST_F(ThreadConfigTest, SchedulerParamsFIFO)
{
  auto params = scheduler_parameters::create_for_policy(
      native_scheduling_policy::fifo, native_thread_priority::highest());

  // May fail if we don't have permissions
  if (params.has_value())
    {
      EXPECT_GT(params.value().sched_priority, 0);
    }
}

TEST_F(ThreadConfigTest, SchedulerParamsFIFOMapsNiceSemanticsToNativePriority)
{
  auto highest = scheduler_parameters::create_for_policy(
      native_scheduling_policy::fifo, native_thread_priority::highest());
  auto lowest = scheduler_parameters::create_for_policy(
      native_scheduling_policy::fifo, native_thread_priority::lowest());

  ASSERT_TRUE(highest.has_value());
  ASSERT_TRUE(lowest.has_value());
  EXPECT_GT(highest.value().sched_priority, lowest.value().sched_priority);
}

TEST_F(ThreadConfigTest, SchedulerParamsFIFOAcceptsNativeRealtimePriorityRange)
{
  int const min_priority = sched_get_priority_min(
      static_cast<int>(native_scheduling_policy::fifo));
  int const max_priority = sched_get_priority_max(
      static_cast<int>(native_scheduling_policy::fifo));

  auto lowest = scheduler_parameters::create_for_policy(
      native_scheduling_policy::fifo,
      native_thread_priority::realtime_lowest());
  auto middle = scheduler_parameters::create_for_policy(
      native_scheduling_policy::fifo, native_thread_priority{ 50 });
  auto highest = scheduler_parameters::create_for_policy(
      native_scheduling_policy::fifo,
      native_thread_priority::realtime_highest());

  ASSERT_TRUE(lowest.has_value());
  ASSERT_TRUE(middle.has_value());
  ASSERT_TRUE(highest.has_value());
  EXPECT_EQ(lowest.value().sched_priority, min_priority);
  EXPECT_EQ(middle.value().sched_priority,
            std::clamp(50, min_priority, max_priority));
  EXPECT_EQ(highest.value().sched_priority, max_priority);
}
#endif

TEST_F(ThreadConfigTest, SchedulingFactoriesResolveToUnifiedSemantics)
{
  auto const background = detail::resolve_scheduling_config(
      detail::native_schedule::background());
  EXPECT_EQ(background.policy, native_scheduling_policy::idle);
  EXPECT_EQ(background.priority.value(),
            native_thread_priority::lowest().value());

  auto const low_latency = detail::resolve_scheduling_config(
      detail::native_schedule::low_latency());
  EXPECT_EQ(low_latency.policy, native_scheduling_policy::other);
  EXPECT_EQ(low_latency.priority.value(),
            native_thread_priority::highest().value());

  auto const realtime = detail::resolve_scheduling_config(
      detail::native_schedule::realtime_rr(99));
  EXPECT_EQ(realtime.policy, native_scheduling_policy::rr);
  EXPECT_EQ(realtime.priority.value(),
            native_thread_priority::realtime_highest().value());

  auto const nice = detail::resolve_scheduling_config(
      detail::native_schedule::posix_nice(19));
  EXPECT_EQ(nice.policy, native_scheduling_policy::other);
  EXPECT_EQ(nice.priority.value(), native_thread_priority::lowest().value());
  EXPECT_EQ(nice.model, native_priority_model::posix_nice);
  EXPECT_TRUE(nice.valid);

  auto const invalid = detail::resolve_scheduling_config(
      detail::native_schedule::posix_nice(20));
  EXPECT_FALSE(invalid.valid);
}

TEST_F(ThreadConfigTest, ThreadConfigAppliesThroughThreadAndPool)
{
  native_thread_config config{};
  config.scheduling = detail::native_schedule::normal();

  detail::thread_backend thread(
      [] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
  auto thread_result = thread.configure(config);
  EXPECT_TRUE(thread_result.has_value()) << thread_result.error().message();
  thread.join();

  thread_pool_backend pool(2);
  auto pool_result = pool.configure_threads(config);
  EXPECT_TRUE(pool_result.has_value()) << pool_result.error().message();
  pool.shutdown(shutdown_policy_backend::drain);
}

// ==================== Integration Tests ====================

TEST_F(ThreadConfigTest, ApplyConfigToThread)
{
  std::atomic<bool> executed{ false };

  detail::thread_backend thread(
      [&executed]()
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          executed = true;
        });

  // Try to configure the thread (may fail without permissions)
  [[maybe_unused]] auto name_result = thread.set_name("test_config");
  [[maybe_unused]] auto priority_result
      = thread.set_priority(native_thread_priority::normal());

  native_thread_affinity affinity;
  affinity.add_cpu(0);
  [[maybe_unused]] auto affinity_result = thread.set_affinity(affinity);

  thread.join();
  EXPECT_TRUE(executed);
}

TEST_F(ThreadConfigTest, ThreadConfigWithSchedulingPolicy)
{
  std::atomic<bool> executed{ false };

  detail::thread_backend thread(
      [&executed]()
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          executed = true;
        });

  // Try to set scheduling policy (may fail without permissions)
  [[maybe_unused]] auto scheduling_result = thread.set_scheduling_policy(
      native_scheduling_policy::other, native_thread_priority::normal());

  // Just ensure it doesn't crash
  thread.join();
  EXPECT_TRUE(executed);
}

TEST_F(ThreadConfigTest, ThreadInfoDefaultConstructorTargetsCurrentThread)
{
  struct Result
  {
    native_thread_id fromConstructor{};
    native_thread_id fromStatic{};
    expected<std::string, std::error_code> name{ threadschedule::unexpected(
        std::make_error_code(std::errc::state_not_recoverable)) };
    std::optional<native_scheduling_policy> policy;
    std::optional<int> priority;
  };

  std::promise<Result> result;

  std::thread thread(
      [&result]()
        {
          thread_info info;
          Result payload;
          payload.fromConstructor = info.thread_id();
          payload.fromStatic = thread_info::get_thread_id();
          payload.name = info.get_name();
          payload.policy = info.get_policy();
          payload.priority = info.get_priority();
          result.set_value(std::move(payload));
        });

  auto const payload = result.get_future().get();
  thread.join();

  EXPECT_EQ(payload.fromConstructor, payload.fromStatic);
  EXPECT_TRUE(payload.name.has_value());
  EXPECT_TRUE(payload.policy.has_value());
  EXPECT_TRUE(payload.priority.has_value());
}

TEST_F(ThreadConfigTest, ThreadInfoExplicitConstructorCanControlTargetThread)
{
  std::promise<native_thread_id> started;
  std::promise<void> release;
  auto release_future = release.get_future().share();

  std::thread thread(
      [&started, release_future]() mutable
        {
          started.set_value(thread_info::get_thread_id());
          release_future.wait();
        });

  native_thread_id const tid = started.get_future().get();
  thread_info info(tid);

  EXPECT_EQ(info.thread_id(), tid);
  ASSERT_TRUE(info.set_name("ti_remote").has_value());

  auto const name = info.get_name();
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(name.value(), "ti_remote");
  EXPECT_TRUE(info.get_policy().has_value());
  EXPECT_TRUE(info.get_priority().has_value());

  release.set_value();
  thread.join();
}

TEST_F(ThreadConfigTest, ThreadInfoInvalidTargetReturnsNoProcess)
{
#ifdef _WIN32
  thread_info info(native_thread_id{ 0 });
#else
  thread_info info(static_cast<native_thread_id>(-1));
#endif

  EXPECT_FALSE(info.get_name().has_value());
  EXPECT_FALSE(info.get_affinity().has_value());
  EXPECT_FALSE(info.get_policy().has_value());
  EXPECT_FALSE(info.get_priority().has_value());
  EXPECT_FALSE(info.set_name("invalid_tid").has_value());
}

// ==================== Nice Value Tests ====================

TEST_F(ThreadConfigTest, NiceValue)
{
  std::promise<void> release;
  auto ready = release.get_future().share();
  threadschedule::thread worker([ready] { ready.wait(); });

  auto set = worker.set_nice(10);
  auto priority = worker.get_priority();
  release.set_value();
  auto joined = worker.join();

  ASSERT_TRUE(set.has_value()) << set.error().message();
  ASSERT_TRUE(priority.has_value()) << priority.error().message();
  EXPECT_EQ(priority.value(), threadschedule::priority_level::lowest);
  EXPECT_TRUE(joined.has_value());
}
