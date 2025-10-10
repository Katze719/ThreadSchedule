#include <chrono>
#include <iostream>
#include <library_ra/library_ra.hpp>
#include <library_rb/library_rb.hpp>
#include <string>
#include <thread>
#include <threadschedule/thread_registry.hpp>
#include <vector>

using namespace threadschedule;

int main()
{
    std::cout << "\n=== Runtime Single Registry Integration Test ===\n";

    // Start workers from both DSOs; they register into the global runtime registry()
    runtime_libA::start_worker("ra-1");
    runtime_libA::start_worker("ra-2");
    runtime_libB::start_worker("rb-1");
    runtime_libB::start_worker("rb-2");

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Validate that registry() sees all 4 threads
    int total = 0;
    int a = 0;
    int b = 0;
    registry().for_each([&](RegisteredThreadInfo const& info) {
        total++;
        if (info.componentTag == "RuntimeLibA")
            a++;
        if (info.componentTag == "RuntimeLibB")
            b++;
    });

    std::cout << "Total threads in runtime registry: " << total << "\n";

    bool success = true;
    if (total != 4)
    {
        std::cerr << "ERROR: Expected 4 total threads, got " << total << "\n";
        success = false;
    }
    if (a != 2)
    {
        std::cerr << "ERROR: Expected 2 threads from RuntimeLibA, got " << a << "\n";
        success = false;
    }
    if (b != 2)
    {
        std::cerr << "ERROR: Expected 2 threads from RuntimeLibB, got " << b << "\n";
        success = false;
    }

    runtime_libA::wait_for_threads();
    runtime_libB::wait_for_threads();

    if (success)
    {
        std::cout << "Runtime single registry test PASSED!\n";
        return 0;
    }
    else
    {
        std::cerr << "Runtime single registry test FAILED!\n";
        return 1;
    }
}
