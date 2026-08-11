#include <threadschedule/advanced.hpp>

#include <gtest/gtest.h>

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

TEST(AdvancedApi, UmbrellaExposesOptionalFacilities)
{
  static_assert(std::is_same_v<threadschedule::advanced::cpu_topology,
                               threadschedule::cpu_topology>);
  static_assert(std::is_same_v<threadschedule::advanced::error_handler,
                               threadschedule::error_handler_backend>);
  static_assert(std::is_same_v<threadschedule::advanced::chaos_config,
                               threadschedule::chaos_config>);

  auto topology = threadschedule::advanced::read_topology();
  EXPECT_GE(topology.cpu_count, 1);

  std::vector<std::future<int>> futures;
  futures.push_back(std::async(std::launch::deferred, [] { return 7; }));
  auto values = threadschedule::advanced::when_all(futures);
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values.front(), 7);
}

TEST(AdvancedApi, NegativeNumaThreadIndexWrapsWithinNode)
{
  threadschedule::cpu_topology topology;
  topology.cpu_count = 3;
  topology.numa_nodes = 1;
  topology.node_to_cpus = { { 2, 4, 6 } };

  auto affinity = threadschedule::affinity_for_node(topology, 0, -1, 2);
  EXPECT_EQ(affinity.get_cpus(), (std::vector<int>{ 2, 6 }));
}

TEST(AdvancedApi, ErrorHandledTaskAcceptsLvalueCallable)
{
  auto handler = std::make_shared<threadschedule::advanced::error_handler>();
  bool ran = false;
  auto callable = [&ran] { ran = true; };

  auto task = threadschedule::advanced::make_error_handled_task(
      callable, std::move(handler));
  task();

  EXPECT_TRUE(ran);
}

#ifdef _WIN32
TEST(AdvancedApi, WindowsProfilesUseSafeDocumentedPriorities)
{
  auto const realtime = threadschedule::profiles::realtime();
  auto realtime_params
      = threadschedule::scheduler_parameters::create_for_policy(
          realtime.policy, realtime.priority);
  ASSERT_TRUE(realtime_params.has_value());
  EXPECT_EQ(realtime_params->sched_priority, THREAD_PRIORITY_HIGHEST);

  auto const throughput = threadschedule::profiles::throughput();
  auto throughput_params
      = threadschedule::scheduler_parameters::create_for_policy(
          throughput.policy, throughput.priority);
  ASSERT_TRUE(throughput_params.has_value());
  EXPECT_EQ(throughput_params->sched_priority, THREAD_PRIORITY_BELOW_NORMAL);
}
#endif

#ifndef _WIN32
TEST(AdvancedApi, ProfilesPreservePosixNicePriorityModel)
{
  using namespace threadschedule;

  auto make_profile = [](int nice_value)
    {
      return thread_profile{ "nice", native_scheduling_policy::other,
                             native_thread_priority{ nice_value },
                             std::nullopt, native_priority_model::posix_nice };
    };

  std::promise<void> backend_release;
  auto backend_ready = backend_release.get_future().share();
  detail::thread_backend backend([backend_ready] { backend_ready.wait(); });
  auto backend_initial = backend.get_nice_value();
  if (!backend_initial)
    {
      backend_release.set_value();
      backend.join();
      FAIL() << backend_initial.error().message();
    }
  if (*backend_initial >= 19)
    {
      backend_release.set_value();
      backend.join();
      GTEST_SKIP() << "Cannot lower a thread already at nice value 19";
    }

  int const backend_target = *backend_initial + 1;
  auto backend_results
      = apply_profile_detailed(backend, make_profile(backend_target));
  auto backend_effective = backend.get_nice_value();
  backend_release.set_value();
  backend.join();

  ASSERT_EQ(backend_results.size(), 1u);
  EXPECT_FALSE(backend_results.front());
  ASSERT_TRUE(backend_effective.has_value());
  EXPECT_EQ(*backend_effective, backend_target);

  thread_registry_backend registry;
  std::promise<native_thread_id> registered;
  std::promise<void> registry_release;
  auto registry_ready = registry_release.get_future().share();
  std::thread registry_thread(
      [&registry, &registered, registry_ready]
        {
          auto control = thread_control_block::create_for_current_thread();
          registry.register_current_thread(control, "profile", "test");
          registered.set_value(control->tid());
          registry_ready.wait();
          registry.unregister_current_thread();
        });

  native_thread_id const tid = registered.get_future().get();
  auto registry_initial = registry.get_nice_value(tid);
  if (!registry_initial)
    {
      registry_release.set_value();
      registry_thread.join();
      FAIL() << registry_initial.error().message();
    }
  if (*registry_initial >= 18)
    {
      registry_release.set_value();
      registry_thread.join();
      GTEST_SKIP() << "Insufficient nice-value range for registry checks";
    }

  int const registry_target = *registry_initial + 1;
  auto registry_result
      = apply_profile(registry, tid, make_profile(registry_target));
  auto registry_effective = registry.get_nice_value(tid);

  auto entry = registry.get(tid);
  if (!entry || !entry->control)
    {
      registry_release.set_value();
      registry_thread.join();
      FAIL() << "Registered thread has no control block";
    }
  int const control_target = registry_target + 1;
  auto control_results
      = apply_profile_detailed(*entry->control, make_profile(control_target));
  auto control_effective = entry->control->get_nice_value();

  registry_release.set_value();
  registry_thread.join();

  ASSERT_TRUE(registry_result.has_value())
      << registry_result.error().message();
  ASSERT_TRUE(registry_effective.has_value());
  EXPECT_EQ(*registry_effective, registry_target);
  ASSERT_EQ(control_results.size(), 1u);
  EXPECT_FALSE(control_results.front());
  ASSERT_TRUE(control_effective.has_value());
  EXPECT_EQ(*control_effective, control_target);
}
#endif
