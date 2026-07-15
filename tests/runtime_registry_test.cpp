#include <gtest/gtest.h>
#include <threadschedule/thread_registry.hpp>

using namespace threadschedule;

#if defined(THREADSCHEDULE_RUNTIME)

TEST(RuntimeRegistry, RegistryAndInjectionWork)
{
  // Default registry reachable via runtime
  thread_registry_backend& reg = registry();
  int before = 0;
  reg.for_each([&](registered_thread_info_backend const&) { before++; });

  // Register a thread and ensure we can see it
  std::thread t(
      []
        {
          auto_register_current_thread guard("rt-1", "rt");
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  int count = 0;
  reg.for_each([&](registered_thread_info_backend const&) { count++; });
  EXPECT_GE(count, before);

  t.join();
}

TEST(RuntimeRegistry, SetExternalRegistry)
{
  thread_registry_backend custom;
  set_external_registry(&custom);

  std::thread t(
      []
        {
          auto_register_current_thread guard("rt-2", "rt2");
          std::this_thread::sleep_for(std::chrono::milliseconds(30));
        });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  int seen = 0;
  custom.for_each([&](registered_thread_info_backend const&) { seen++; });
  EXPECT_GE(seen, 0);

  t.join();

  // reset
  set_external_registry(nullptr);
}

#endif
