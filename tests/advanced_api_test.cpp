#include <threadschedule/advanced.hpp>

#include <gtest/gtest.h>

#include <future>
#include <memory>
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
