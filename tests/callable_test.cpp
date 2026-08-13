#include <gtest/gtest.h>
#include <threadschedule/advanced/error_handler.hpp>
#include <threadschedule/detail/callable/function_ref.hpp>
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;
using namespace threadschedule::advanced;
using namespace threadschedule::detail;

namespace
{

auto
increment(int value) -> int
{
  return value + 1;
}

} // namespace

TEST(CallableTest, FunctionRefWrapsFunctionPointer)
{
  detail::function_ref<int(int)> ref = increment;
  EXPECT_EQ(ref(41), 42);
}

TEST(CallableTest, FunctionRefWrapsLambdaReference)
{
  int sum = 0;
  auto lambda = [&sum](int value)
    {
      sum += value;
      return sum;
    };

  detail::function_ref<int(int)> ref = lambda;

  EXPECT_EQ(ref(3), 3);
  EXPECT_EQ(ref(4), 7);
}

TEST(CallableTest, PublicCallbackAliasesAcceptLambdas)
{
  task_start_callback on_start = [](std::chrono::steady_clock::time_point, std::thread::id) {};
  threadschedule::advanced::error_callback on_error = [](threadschedule::advanced::task_error const&) {};

  EXPECT_TRUE(static_cast<bool>(on_start));
  EXPECT_TRUE(static_cast<bool>(on_error));
}

TEST(CallableTest, MoveCallableAcceptsMoveOnlyTargetsInCxx17)
{
  auto payload = std::make_unique<int>(42);
  detail::move_callable<int()> callable([payload = std::move(payload)] { return *payload; });

  static_assert(!std::is_copy_constructible_v<decltype(callable)>);
  EXPECT_EQ(callable(), 42);
}
