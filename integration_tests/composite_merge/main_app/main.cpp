#include <cassert>
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

    composite_libA::get_registry().for_each([&](RegisteredThreadInfo const& info) {
        a_count++;
        assert(info.componentTag == "CompositeLibA");
    });

    composite_libB::get_registry().for_each([&](RegisteredThreadInfo const& info) {
        b_count++;
        assert(info.componentTag == "CompositeLibB");
    });

    std::cout << "LibA registry: " << a_count << " threads\n";
    std::cout << "LibB registry: " << b_count << " threads\n";

    assert(a_count == 2 && "Expected 2 threads in LibA registry");
    assert(b_count == 2 && "Expected 2 threads in LibB registry");

    // Merge library-local registries via composite view
    CompositeThreadRegistry comp;
    comp.attach(&composite_libA::get_registry());
    comp.attach(&composite_libB::get_registry());

    int total = 0;
    comp.for_each([&](RegisteredThreadInfo const&) { total++; });

    std::cout << "Composite registry: " << total << " threads\n";
    assert(total == 4 && "Expected 4 threads in composite registry");

    composite_libA::wait_for_threads();
    composite_libB::wait_for_threads();

    std::cout << "Composite merge scenario executed.\n";
    return 0;
}
