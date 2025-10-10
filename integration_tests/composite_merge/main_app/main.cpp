#include <chrono>
#include <iostream>
#include <library_ca/library_ca.hpp>
#include <library_cb/library_cb.hpp>
#include <string>
#include <thread>
#include <threadschedule/thread_registry.hpp>

using namespace threadschedule;

int main()
{
    std::cout << "\n=== Composite Merge Integration Test ===\n";

    composite_libA::start_worker("ca-1");
    composite_libA::start_worker("ca-2");
    composite_libB::start_worker("cb-1");
    composite_libB::start_worker("cb-2");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Verify individual library registries
    int a_count = 0;
    int b_count = 0;
    bool success = true;

    composite_libA::get_registry().for_each([&](RegisteredThreadInfo const& info) {
        a_count++;
        if (info.componentTag != "CompositeLibA")
        {
            std::cerr << "ERROR: LibA thread has wrong tag: " << info.componentTag << "\n";
            success = false;
        }
    });

    composite_libB::get_registry().for_each([&](RegisteredThreadInfo const& info) {
        b_count++;
        if (info.componentTag != "CompositeLibB")
        {
            std::cerr << "ERROR: LibB thread has wrong tag: " << info.componentTag << "\n";
            success = false;
        }
    });

    std::cout << "LibA registry: " << a_count << " threads\n";
    std::cout << "LibB registry: " << b_count << " threads\n";

    if (a_count != 2)
    {
        std::cerr << "ERROR: Expected 2 threads in LibA registry, got " << a_count << "\n";
        success = false;
    }
    if (b_count != 2)
    {
        std::cerr << "ERROR: Expected 2 threads in LibB registry, got " << b_count << "\n";
        success = false;
    }

    // Merge library-local registries via composite view
    CompositeThreadRegistry comp;
    comp.attach(&composite_libA::get_registry());
    comp.attach(&composite_libB::get_registry());

    int total = 0;
    comp.for_each([&](RegisteredThreadInfo const&) { total++; });

    std::cout << "Composite registry: " << total << " threads\n";

    if (total != 4)
    {
        std::cerr << "ERROR: Expected 4 threads in composite registry, got " << total << "\n";
        success = false;
    }

    composite_libA::wait_for_threads();
    composite_libB::wait_for_threads();

    if (success)
    {
        std::cout << "Composite merge scenario PASSED.\n";
        return 0;
    }
    else
    {
        std::cerr << "Composite merge scenario FAILED.\n";
        return 1;
    }
}
