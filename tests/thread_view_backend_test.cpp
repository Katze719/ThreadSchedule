#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <threadschedule/detail/thread/identity.hpp>
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;
using namespace threadschedule::detail;

class ThreadViewBackendTest : public ::testing::Test
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

TEST_F(ThreadViewBackendTest, WrapExistingStdThreadAndSetName)
{
  std::thread t([] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });

  detail::thread_view_backend view(t);
  auto r = view.set_name("view_thread");
  (void)r;

  EXPECT_TRUE(view.joinable());
  view.join();
  EXPECT_FALSE(view.joinable());
}

TEST_F(ThreadViewBackendTest, ViewDoesNotOwnLifetime)
{
  std::atomic<bool> ran{ false };
  std::thread t(
      [&]
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          ran = true;
        });

  {
    detail::thread_view_backend view(t);
    EXPECT_TRUE(view.joinable());
  } // view destroyed, thread still running/owned by t

  t.join();
  EXPECT_TRUE(ran);
}

#ifndef _WIN32
TEST_F(ThreadViewBackendTest, NativeThreadIdentityRejectsMismatchedGeneration)
{
  detail::native_thread_identity identity;
  identity.id = detail::current_native_thread_id();
  auto const start_time = detail::read_thread_start_time(identity.id);
  ASSERT_TRUE(start_time.has_value());

  identity.start_time = start_time.value() + 1;
  EXPECT_FALSE(detail::native_thread_is_alive(identity));
}
#endif

TEST_F(ThreadViewBackendTest, ImplicitConversionFromStdThread)
{
  std::atomic<bool> ran{ false };
  std::thread t([&] { ran = true; });

  // foo takes a view by value; implicit conversion from std::thread& should
  // work
  auto foo = [](detail::thread_view_backend v)
    {
      EXPECT_TRUE(v.joinable());
      v.join();
    };
  foo(t);
  EXPECT_TRUE(ran.load());
}
