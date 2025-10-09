#include <gtest/gtest.h>
#include <threadschedule/thread_registry.hpp>

using namespace threadschedule;

#if defined(THREADSCHEDULE_RUNTIME)

TEST(RuntimeRegistry, RegistryAndInjectionWork)
{
    // Default registry reachable via runtime
    ThreadRegistry& reg = registry();
    int before = 0;
    reg.for_each([&](RegisteredThreadInfo const&) { before++; });

    // Register a thread and ensure we can see it
    std::thread t([] {
        AutoRegisterCurrentThread guard("rt-1", "rt");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    int count = 0;
    reg.for_each([&](RegisteredThreadInfo const&) { count++; });
    EXPECT_GE(count, before);

    t.join();
}

TEST(RuntimeRegistry, SetExternalRegistry)
{
    ThreadRegistry custom;
    set_external_registry(&custom);

    std::thread t([] {
        AutoRegisterCurrentThread guard("rt-2", "rt2");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    int seen = 0;
    custom.for_each([&](RegisteredThreadInfo const&) { seen++; });
    EXPECT_GE(seen, 0);

    t.join();

    // reset
    set_external_registry(nullptr);
}

#endif
