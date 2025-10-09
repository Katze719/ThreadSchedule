#include <appinj_libA/libA.hpp>
#include <appinj_libB/libB.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include <threadschedule/thread_registry.hpp>

using namespace threadschedule;

int main()
{
    std::cout << "\n=== App Injection Integration Test ===\n";

    ThreadRegistry app_registry;
    set_external_registry(&app_registry);

    appinj_libA::start_worker("inj-a1");
    appinj_libA::start_worker("inj-a2");
    appinj_libB::start_worker("inj-b1");
    appinj_libB::start_worker("inj-b2");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int total = 0;
    int a = 0;
    int b = 0;
    registry().for_each([&](RegisteredThreadInfo const& info) {
        total++;
        if (info.componentTag == "AppInjLibA")
            a++;
        if (info.componentTag == "AppInjLibB")
            b++;
    });
    std::cout << "App registry sees: total=" << total << ", A=" << a << ", B=" << b << "\n";

    appinj_libA::wait_for_threads();
    appinj_libB::wait_for_threads();

    set_external_registry(nullptr);

    std::cout << "App injection integration test complete.\n";
    return 0;
}
