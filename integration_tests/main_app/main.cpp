#include <cassert>
#include <chrono>
#include <iostream>
#include <library_a/library_a.hpp>
#include <library_b/library_b.hpp>
#include <string>
#include <thread>
#include <threadschedule/thread_registry.hpp>
#include <vector>

using namespace threadschedule;

void test_isolated_registries()
{
    std::cout << "\n=== Test 1: Isolated Registries with Composite Merge ===" << std::endl;

    // Start workers in both libraries (each uses its own registry)
    library_a::start_worker("worker-a1");
    library_a::start_worker("worker-a2");
    library_b::start_worker("worker-b1");
    library_b::start_worker("worker-b2");

    // Give threads time to register
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Each library should have its own threads
    int count_a = library_a::get_thread_count();
    int count_b = library_b::get_thread_count();

    std::cout << "Library A has " << count_a << " threads" << std::endl;
    std::cout << "Library B has " << count_b << " threads" << std::endl;

    assert(count_a == 2 && "Library A should have 2 threads");
    assert(count_b == 2 && "Library B should have 2 threads");

    // Create a composite view to see all threads
    CompositeThreadRegistry composite;
    composite.attach(&library_a::get_registry());
    composite.attach(&library_b::get_registry());

    std::vector<std::string> tags;
    composite.for_each([&](RegisteredThreadInfo const& info) {
        std::cout << "  Thread: " << info.name << " (tag: " << info.componentTag << ")" << std::endl;
        tags.push_back(info.componentTag);
    });

    assert(tags.size() == 4 && "Composite should see all 4 threads");

    // Apply operation across all registries
    int applied = 0;
    composite.apply(
        [](RegisteredThreadInfo const& e) { return e.componentTag == "LibraryA" || e.componentTag == "LibraryB"; },
        [&](RegisteredThreadInfo const&) { applied++; });

    std::cout << "Applied operation to " << applied << " threads" << std::endl;
    assert(applied == 4 && "Should apply to all 4 threads");

    // Wait for threads to complete
    library_a::wait_for_threads();
    library_b::wait_for_threads();

    std::cout << "✓ Test 1 passed!" << std::endl;
}

void test_shared_registry()
{
    std::cout << "\n=== Test 2: Shared Registry (App Injection Pattern) ===" << std::endl;

    // Create an app-owned registry
    ThreadRegistry app_registry;

    // Inject it into both libraries
    library_a::set_registry(&app_registry);
    library_b::set_registry(&app_registry);

    // Start workers in both libraries
    library_a::start_worker("shared-a1");
    library_a::start_worker("shared-a2");
    library_b::start_worker("shared-b1");
    library_b::start_worker("shared-b2");

    // Give threads time to register
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // All threads should be in the app registry
    int total_count = 0;
    std::vector<std::string> tags;
    app_registry.for_each([&](RegisteredThreadInfo const& info) {
        std::cout << "  Thread: " << info.name << " (tag: " << info.componentTag << ")" << std::endl;
        total_count++;
        tags.push_back(info.componentTag);
    });

    std::cout << "App registry has " << total_count << " threads total" << std::endl;
    assert(total_count == 4 && "App registry should have all 4 threads");

    // Verify we have threads from both libraries
    int libA_count = 0, libB_count = 0;
    for (auto const& tag : tags)
    {
        if (tag == "LibraryA")
            libA_count++;
        if (tag == "LibraryB")
            libB_count++;
    }

    assert(libA_count == 2 && "Should have 2 threads from Library A");
    assert(libB_count == 2 && "Should have 2 threads from Library B");

    // Wait for threads to complete
    library_a::wait_for_threads();
    library_b::wait_for_threads();

    // Reset to local registries
    library_a::set_registry(nullptr);
    library_b::set_registry(nullptr);

    std::cout << "✓ Test 2 passed!" << std::endl;
}

void test_concurrent_operations()
{
    std::cout << "\n=== Test 3: Concurrent Registry Operations ===" << std::endl;

    ThreadRegistry shared_reg;
    library_a::set_registry(&shared_reg);
    library_b::set_registry(&shared_reg);

    // Start many workers concurrently
    for (int i = 0; i < 5; i++)
    {
        library_a::start_worker(("concurrent-a" + std::to_string(i)).c_str());
        library_b::start_worker(("concurrent-b" + std::to_string(i)).c_str());
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int count = 0;
    shared_reg.for_each([&](RegisteredThreadInfo const&) { count++; });

    std::cout << "Concurrent test has " << count << " threads" << std::endl;
    assert(count == 10 && "Should have 10 concurrent threads");

    library_a::wait_for_threads();
    library_b::wait_for_threads();

    library_a::set_registry(nullptr);
    library_b::set_registry(nullptr);

    std::cout << "✓ Test 3 passed!" << std::endl;
}

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "Thread Registry Integration Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    try
    {
        test_isolated_registries();
        test_shared_registry();
        test_concurrent_operations();

        std::cout << "\n========================================" << std::endl;
        std::cout << "✓ All tests passed!" << std::endl;
        std::cout << "========================================" << std::endl;

        return 0;
    }
    catch (std::exception const& e)
    {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "\n✗ Test failed with unknown exception" << std::endl;
        return 1;
    }
}
