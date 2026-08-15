#include <threadschedule/cpu_id.hpp>
#include <threadschedule/detail/callable/move_only_function.hpp>
#include <threadschedule/detail/scope_exit.hpp>
#include <threadschedule/detail/try_result.hpp>
#include <threadschedule/nice_value.hpp>
#include <threadschedule/realtime_priority.hpp>
#include <threadschedule/thread_id.hpp>
#include <threadschedule/worker_count.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <type_traits>

#ifdef _WIN32
#  include <threadschedule/detail/unique_handle.hpp>
#  include <windows.h>
#endif

namespace
{

struct large_callable
{
  std::array<std::byte, 256> storage{};
  std::unique_ptr<int> value{ std::make_unique<int>(42) };

  auto
  operator()(int increment) -> int
  {
    return *value + increment;
  }
};

struct alignas(64) over_aligned_callable
{
  int* calls;
  void
  operator()()
  {
    ++*calls;
  }
};

} // namespace

TEST(FoundationTest, ScopeExitRunsExactlyOnceUnlessReleased)
{
  int calls = 0;
  {
    auto guard = threadschedule::detail::make_scope_exit([&calls] { ++calls; });
    (void)guard;
  }
  EXPECT_EQ(calls, 1);

  {
    auto guard = threadschedule::detail::make_scope_exit([&calls] { ++calls; });
    guard.release();
  }
  EXPECT_EQ(calls, 1);
}

TEST(FoundationTest, MoveOnlyFunctionSupportsEmptySmallLargeAndOverAlignedTargets)
{
  using function_type = threadschedule::detail::move_only_function<int(int)>;
  static_assert(!std::is_copy_constructible_v<function_type>);
  static_assert(std::is_nothrow_move_constructible_v<function_type>);

  function_type empty;
  EXPECT_FALSE(empty);
  EXPECT_THROW((void)empty(1), std::bad_function_call);

  auto small_callable = [value = std::make_unique<int>(40)](int increment) { return *value + increment; };
  function_type small(std::move(small_callable));
  EXPECT_EQ(small(2), 42);

  function_type large = large_callable{};
  EXPECT_EQ(large(1), 43);

  auto tiny_storage = threadschedule::detail::make_move_only_function<int(), 1>([]() { return 42; });
  EXPECT_EQ(tiny_storage(), 42);

  int calls = 0;
  auto over_aligned = threadschedule::detail::make_move_only_function<void(), 128>(over_aligned_callable{ &calls });
  over_aligned();
  EXPECT_EQ(calls, 1);

  function_type moved(std::move(small));
  // The empty moved-from state is part of move_only_function's contract.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_FALSE(small);
  EXPECT_EQ(moved(3), 43);
}

TEST(FoundationTest, MoveOnlyFunctionPropagatesTargetExceptions)
{
  threadschedule::detail::move_only_function<void()> function = [] { throw std::runtime_error("boom"); };
  EXPECT_THROW(function(), std::runtime_error);
}

TEST(FoundationTest, TryResultMapsKnownAndUnknownExceptions)
{
  auto system = threadschedule::detail::try_result(
      []() -> threadschedule::result<void>
        { throw std::system_error(std::make_error_code(std::errc::permission_denied)); });
  ASSERT_FALSE(system.has_value());
  EXPECT_EQ(system.error(), std::make_error_code(std::errc::permission_denied));

  auto unknown
      = threadschedule::detail::try_result([]() -> threadschedule::result<void> { throw std::runtime_error("boom"); });
  ASSERT_FALSE(unknown.has_value());
  EXPECT_EQ(unknown.error(), std::make_error_code(std::errc::state_not_recoverable));
}

TEST(FoundationTest, StrongValuesRejectImplicitAndInvalidIntegerUse)
{
  static_assert(!std::is_convertible_v<int, threadschedule::cpu_id>);
  static_assert(!std::is_convertible_v<int, threadschedule::nice_value>);
  static_assert(!std::is_convertible_v<int, threadschedule::realtime_priority>);
  static_assert(!std::is_convertible_v<int, threadschedule::thread_id>);
  static_assert(!std::is_convertible_v<std::size_t, threadschedule::worker_count>);

  EXPECT_FALSE(threadschedule::cpu_id::create(-1).has_value());
  EXPECT_FALSE(threadschedule::nice_value::create(20).has_value());
  EXPECT_FALSE(threadschedule::realtime_priority::create(0).has_value());
  EXPECT_FALSE(threadschedule::thread_id::create(0).has_value());
  EXPECT_FALSE(threadschedule::worker_count::create(0).has_value());
  EXPECT_THROW((void)threadschedule::worker_count{ 0 }, std::invalid_argument);

  auto automatic = threadschedule::worker_count::automatic();
  EXPECT_TRUE(automatic.is_automatic());
  EXPECT_GE(automatic.resolve(), 1u);
  EXPECT_EQ(threadschedule::worker_count{ 3 }.resolve(), 3u);
}

#ifdef _WIN32
TEST(FoundationTest, UniqueHandleMovesReleasesAndResetsOwnership)
{
  HANDLE released = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(released, nullptr);
  {
    threadschedule::detail::unique_handle owner(released);
    EXPECT_EQ(owner.release(), released);
    EXPECT_FALSE(owner);
  }
  EXPECT_NE(CloseHandle(released), 0);

  HANDLE first = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  HANDLE second = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  threadschedule::detail::unique_handle owner(first);
  owner.reset(second);
  EXPECT_EQ(SetEvent(first), 0);

  threadschedule::detail::unique_handle moved(std::move(owner));
  EXPECT_FALSE(owner);
  EXPECT_EQ(moved.get(), second);
}
#endif
