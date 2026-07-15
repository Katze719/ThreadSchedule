#include <atomic>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <gtest/gtest.h>
#include <thread>
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

class ThreadBackendTest : public ::testing::Test
{
protected:
  void
  SetUp() override
  {
    // Setup code if needed
  }

  void
  TearDown() override
  {
    // Cleanup code if needed
  }
};

#ifdef _WIN32
namespace
{
enum class ResolverMode
{
  all_exports,
  no_exports,
  set_only,
  kernelbase_only,
  no_modules
};

ResolverMode resolver_mode = ResolverMode::all_exports;

HMODULE WINAPI
fake_module_lookup(LPCWSTR name)
{
  if (resolver_mode == ResolverMode::no_modules)
    return nullptr;
  if (resolver_mode == ResolverMode::kernelbase_only
      && std::wcscmp(name, L"kernel32.dll") == 0)
    return nullptr;
  return reinterpret_cast<HMODULE>(&resolver_mode);
}

INT_PTR WINAPI
fake_export()
{
  return 0;
}

FARPROC WINAPI
fake_proc_lookup(HMODULE, LPCSTR name)
{
  if (resolver_mode == ResolverMode::no_exports)
    return nullptr;
  if (std::strcmp(name, "SetThreadDescription") == 0)
    return fake_export;
  return resolver_mode == ResolverMode::all_exports
                 || resolver_mode == ResolverMode::kernelbase_only
             ? fake_export
             : nullptr;
}
} // namespace

TEST_F(ThreadBackendTest, ThreadDescriptionResolverFindsKernelExports)
{
  resolver_mode = ResolverMode::all_exports;
  auto const resolved = detail::resolve_thread_description_api(
      fake_module_lookup, fake_proc_lookup);
  EXPECT_TRUE(resolved.found_module);
  EXPECT_NE(resolved.set, nullptr);
  EXPECT_NE(resolved.get, nullptr);
}

TEST_F(ThreadBackendTest, ThreadDescriptionResolverReportsMissingExports)
{
  resolver_mode = ResolverMode::no_exports;
  auto const resolved = detail::resolve_thread_description_api(
      fake_module_lookup, fake_proc_lookup);
  EXPECT_TRUE(resolved.found_module);
  EXPECT_EQ(resolved.set, nullptr);
  EXPECT_EQ(resolved.get, nullptr);
}

TEST_F(ThreadBackendTest,
       ThreadDescriptionResolverDistinguishesModuleLookupFailure)
{
  resolver_mode = ResolverMode::no_modules;
  auto const resolved = detail::resolve_thread_description_api(
      fake_module_lookup, fake_proc_lookup);
  EXPECT_FALSE(resolved.found_module);
  EXPECT_EQ(resolved.set, nullptr);
  EXPECT_EQ(resolved.get, nullptr);
  EXPECT_EQ(resolved.lookup_error.category(), std::system_category());
}

TEST_F(ThreadBackendTest,
       ThreadDescriptionResolverAllowsPartiallyAvailableApis)
{
  resolver_mode = ResolverMode::set_only;
  auto const resolved = detail::resolve_thread_description_api(
      fake_module_lookup, fake_proc_lookup);
  EXPECT_TRUE(resolved.found_module);
  EXPECT_NE(resolved.set, nullptr);
  EXPECT_EQ(resolved.get, nullptr);
}

TEST_F(ThreadBackendTest, ThreadDescriptionResolverFallsBackToKernelBase)
{
  resolver_mode = ResolverMode::kernelbase_only;
  auto const resolved = detail::resolve_thread_description_api(
      fake_module_lookup, fake_proc_lookup);
  EXPECT_TRUE(resolved.found_module);
  EXPECT_NE(resolved.set, nullptr);
  EXPECT_NE(resolved.get, nullptr);
}

TEST_F(ThreadBackendTest, HResultErrorsRetainTheirOriginalCategory)
{
  auto const win32_error = detail::error_from_hresult(
      HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER));
  auto const hresult_error = detail::error_from_hresult(E_FAIL);
  EXPECT_EQ(win32_error.value(), ERROR_INVALID_PARAMETER);
  EXPECT_EQ(win32_error.category(), std::system_category());
  EXPECT_NE(hresult_error.category(), std::system_category());
  EXPECT_NE(hresult_error.message().find("0x80004005"), std::string::npos);
}

TEST_F(ThreadBackendTest,
       Utf8ConversionRejectsInvalidInputAndRoundTripsUnicode)
{
  std::string const name = "worker-\xC3\xA4";
  auto const wide = detail::utf8_to_utf16(name);
  ASSERT_TRUE(wide.has_value()) << wide.error().message();
  auto const roundtrip = detail::utf16_to_utf8(wide.value().c_str());
  ASSERT_TRUE(roundtrip.has_value()) << roundtrip.error().message();
  EXPECT_EQ(roundtrip.value(), name);

  auto const invalid = detail::utf8_to_utf16(std::string{ "\xC3" });
  EXPECT_FALSE(invalid.has_value());
}
#endif

// Test basic thread creation and execution
TEST_F(ThreadBackendTest, BasicThreadCreation)
{
  std::atomic<bool> executed{ false };

  detail::thread_backend thread([&executed]() { executed = true; });

  EXPECT_TRUE(thread.joinable());
  thread.join();
  EXPECT_FALSE(thread.joinable());
  EXPECT_TRUE(executed);
}

namespace
{
void
take_std_thread(std::thread t)
{
  t.join();
}

void
take_thread(detail::thread_backend w)
{
  w.join();
}
} // namespace

TEST_F(ThreadBackendTest, ReleaseAsStdThread)
{
  std::atomic<bool> executed{ false };
  detail::thread_backend w([&] { executed = true; });
  take_std_thread(w.release());
  EXPECT_TRUE(executed);
}

TEST_F(ThreadBackendTest, ConstructFromStdThreadRvalue)
{
  std::atomic<bool> executed{ false };
  auto make_thread = [&]() { return std::thread([&] { executed = true; }); };
  take_thread(make_thread());
  EXPECT_TRUE(executed);
}

TEST_F(ThreadBackendTest, ConstructFromMovedStdThread)
{
  std::atomic<bool> executed{ false };
  std::thread t([&] { executed = true; });
  take_thread(std::move(t));
  EXPECT_TRUE(executed);
}

// Test thread with parameters
TEST_F(ThreadBackendTest, ThreadWithParameters)
{
  std::atomic<int> result{ 0 };

  detail::thread_backend thread([&result](int a, int b) { result = a + b; },
                                10, 20);

  thread.join();
  EXPECT_EQ(result, 30);
}

// Test thread with return value via promise/future pattern
TEST_F(ThreadBackendTest, ThreadWithReturnValue)
{
  std::promise<int> promise;
  auto future = promise.get_future();

  detail::thread_backend thread([&promise]() { promise.set_value(42); });

  thread.join();
  EXPECT_EQ(future.get(), 42);
}

// Test thread naming (Windows 10+)
TEST_F(ThreadBackendTest, ThreadNaming)
{
  detail::thread_backend thread(
      []() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

  auto set_name_result = thread.set_name("test_thread");
#ifdef _WIN32
  if (!set_name_result.has_value()
      && set_name_result.error()
             == std::make_error_code(std::errc::function_not_supported))
    GTEST_SKIP()
        << "SetThreadDescription is unavailable on this Windows version";
  ASSERT_TRUE(set_name_result.has_value())
      << set_name_result.error().message();
  auto name = thread.get_name();
  ASSERT_TRUE(name.has_value()) << name.error().message();
  EXPECT_EQ(name.value(), "test_thread");
#else
  // On Linux, naming should work
  ASSERT_TRUE(set_name_result.has_value());
  auto name = thread.get_name();
  ASSERT_TRUE(name.has_value()) << name.error().message();
  EXPECT_EQ(name.value(), "test_thread");
#endif

  thread.join();
}

#ifdef _WIN32
TEST_F(ThreadBackendTest, ThreadNamingRoundTripsUtf8)
{
  detail::thread_backend thread(
      []() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
  std::string const name = "worker-\xC3\xA4";

  auto const set_result = thread.set_name(name);
  if (!set_result.has_value()
      && set_result.error()
             == std::make_error_code(std::errc::function_not_supported))
    GTEST_SKIP()
        << "SetThreadDescription is unavailable on this Windows version";
  ASSERT_TRUE(set_result.has_value()) << set_result.error().message();

  auto const read_result = thread.get_name();
  ASSERT_TRUE(read_result.has_value()) << read_result.error().message();
  EXPECT_EQ(read_result.value(), name);
  thread.join();
}

TEST_F(ThreadBackendTest, ThreadNamingRejectsInvalidUtf8)
{
  detail::thread_backend thread(
      []() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
  std::string const invalid_utf8{ "worker-\xC3" };
  auto const result = thread.set_name(invalid_utf8);
  EXPECT_FALSE(result.has_value());
  if (!result.has_value())
    EXPECT_NE(result.error(),
              std::make_error_code(std::errc::function_not_supported));
  thread.join();
}

TEST_F(ThreadBackendTest, ThreadNamingAllowsEmptyName)
{
  detail::thread_backend thread(
      []() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
  auto const result = thread.set_name("");
  if (!result.has_value()
      && result.error()
             == std::make_error_code(std::errc::function_not_supported))
    GTEST_SKIP()
        << "SetThreadDescription is unavailable on this Windows version";
  ASSERT_TRUE(result.has_value()) << result.error().message();
  auto const read_result = thread.get_name();
  ASSERT_TRUE(read_result.has_value()) << read_result.error().message();
  EXPECT_EQ(read_result.value(), "");
  thread.join();
}
#endif

#ifndef _WIN32
// POSIX: long names (>15) should fail with invalid_argument
TEST_F(ThreadBackendTest, ThreadNamingTooLongFails)
{
  detail::thread_backend thread(
      []() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
  std::string long_name(16, 'x');
  auto res = thread.set_name(long_name);
  EXPECT_FALSE(res.has_value());
  if (!res.has_value())
    {
      EXPECT_EQ(res.error(),
                std::make_error_code(std::errc::invalid_argument));
    }
  thread.join();
}
#endif

// Test thread priority
TEST_F(ThreadBackendTest, native_thread_priority)
{
  detail::thread_backend thread(
      []() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

  // Set priority should not crash
  [[maybe_unused]] bool priority_set
      = thread.set_priority(native_thread_priority::normal()).has_value();
  // Priority setting may fail depending on permissions
  // Just ensure it doesn't crash

  thread.join();
}

// Test thread detach
TEST_F(ThreadBackendTest, ThreadDetach)
{
  std::atomic<bool> executed{ false };

  detail::thread_backend thread(
      [&executed]()
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          executed = true;
        });

  EXPECT_TRUE(thread.joinable());
  thread.detach();
  EXPECT_FALSE(thread.joinable());

  // Wait a bit to ensure thread completes
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_TRUE(executed);
}

// Test multiple threads
TEST_F(ThreadBackendTest, MultipleThreads)
{
  constexpr int num_threads = 10;
  std::atomic<int> counter{ 0 };
  std::vector<std::unique_ptr<detail::thread_backend>> threads;

  for (int i = 0; i < num_threads; ++i)
    {
      threads.push_back(std::make_unique<detail::thread_backend>(
          [&counter]() { counter++; }));
    }

  for (auto& thread : threads)
    {
      thread->join();
    }

  EXPECT_EQ(counter, num_threads);
}

// Test thread exception handling
TEST_F(ThreadBackendTest, ThreadExceptionHandling)
{
  // Thread exceptions are caught by the managed-thread implementation.
  // This test ensures the thread can be joined even if it throws
  std::atomic<bool> thread_started{ false };

  detail::thread_backend thread(
      [&thread_started]()
        {
          thread_started = true;
          // Exception will be caught internally by std::thread
          try
            {
              throw std::runtime_error("Test exception");
            }
          catch (...)
            {
              // Caught internally
            }
        });

  // Join should complete
  EXPECT_NO_THROW(thread.join());
  EXPECT_TRUE(thread_started);
}

// Test move semantics
TEST_F(ThreadBackendTest, MoveSemantics)
{
  std::atomic<bool> executed{ false };

  detail::thread_backend thread1([&executed]() { executed = true; });

  // Move thread1 to thread2
  detail::thread_backend thread2(std::move(thread1));

  // std::thread's move contract leaves the source non-joinable.
  EXPECT_FALSE(thread1.joinable()); // NOLINT(bugprone-use-after-move)
  EXPECT_TRUE(thread2.joinable());

  thread2.join();
  EXPECT_TRUE(executed);
}

// Test thread affinity (if supported)
TEST_F(ThreadBackendTest, native_thread_affinity)
{
  detail::thread_backend thread(
      []() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

  // Try to set affinity to CPU 0
  native_thread_affinity affinity;
  affinity.add_cpu(0);

  [[maybe_unused]] auto affinity_result = thread.set_affinity(affinity);
  // Affinity setting may fail depending on system/permissions
  // Just ensure it doesn't crash

  thread.join();
}

// Test get_id
TEST_F(ThreadBackendTest, GetThreadId)
{
  std::thread::id thread_id;

  detail::thread_backend thread([&thread_id]()
                                  { thread_id = std::this_thread::get_id(); });

  auto managed_id = thread.get_id();
  thread.join();

  EXPECT_NE(managed_id, std::thread::id());
  EXPECT_EQ(managed_id, thread_id);
}

// Performance test - thread creation overhead
TEST_F(ThreadBackendTest, ThreadCreationPerformance)
{
  constexpr int num_iterations = 100;
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < num_iterations; ++i)
    {
      detail::thread_backend thread([]() {});
      thread.join();
    }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration
      = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  // Just ensure it completes in reasonable time (< 1 second for 100 threads)
  EXPECT_LT(duration.count(), 1000000);

  std::cout << "Thread creation avg: " << (duration.count() / num_iterations)
            << " μs/thread" << std::endl;
}
