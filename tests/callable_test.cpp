#include <gtest/gtest.h>
#include <threadschedule/error_handler.hpp>
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

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
  task_start_callback on_start
      = [](std::chrono::steady_clock::time_point, std::thread::id) {};
  error_callback_backend on_error = [](task_error_backend const&) {};

  EXPECT_TRUE(static_cast<bool>(on_start));
  EXPECT_TRUE(static_cast<bool>(on_error));
}
