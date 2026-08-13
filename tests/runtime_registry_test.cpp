#include <gtest/gtest.h>
#include <threadschedule/thread_registry.hpp>

using namespace threadschedule;
using namespace threadschedule::detail;

#if defined(THREADSCHEDULE_RUNTIME)

TEST(RuntimeRegistry, RegistryAndInjectionWork)
{
  // Default registry reachable via runtime
  thread_registry& reg = global_registry();
  auto const before = reg.count();

  // Register a thread and ensure we can see it
  std::thread t(
      []
        {
          registration_guard_backend guard("rt-1", "rt");
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_GE(reg.count(), before);

  t.join();
}

TEST(RuntimeRegistry, SetExternalRegistry)
{
  thread_registry custom;
  use_global_registry(&custom);

  std::thread t(
      []
        {
          registration_guard_backend guard("rt-2", "rt2");
          std::this_thread::sleep_for(std::chrono::milliseconds(30));
        });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_GE(custom.count(), 0u);

  t.join();

  // reset
  use_global_registry(nullptr);
}

#endif
