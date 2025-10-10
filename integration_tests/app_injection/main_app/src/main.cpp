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
    bool success = true;

    // Inject the app's registry into each DSO
    appinj_libA::set_registry(&app_registry);
    appinj_libB::set_registry(&app_registry);

    // Phase 1: Start 4 threads
    std::cout << "\nPhase 1: Starting 4 threads...\n";
    appinj_libA::start_worker("inj-a1");
    appinj_libA::start_worker("inj-a2");
    appinj_libB::start_worker("inj-b1");
    appinj_libB::start_worker("inj-b2");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify all 4 threads are registered
    int total = registry().count();
    int a = registry().filter([](auto const& e) { return e.componentTag == "AppInjLibA"; }).count();
    int b = registry().filter([](auto const& e) { return e.componentTag == "AppInjLibB"; }).count();

    std::cout << "  Registry sees: total=" << total << ", A=" << a << ", B=" << b << "\n";

    if (total != 4)
    {
        std::cerr << "  ERROR: Expected 4 total threads, got " << total << "\n";
        success = false;
    }
    if (a != 2)
    {
        std::cerr << "  ERROR: Expected 2 threads from LibA, got " << a << "\n";
        success = false;
    }
    if (b != 2)
    {
        std::cerr << "  ERROR: Expected 2 threads from LibB, got " << b << "\n";
        success = false;
    }

    // Phase 2: Test any/all/none predicates
    std::cout << "\nPhase 2: Testing query predicates...\n";
    bool has_libA = registry().any([](auto const& e) { return e.componentTag == "AppInjLibA"; });
    bool all_alive = registry().all([](auto const& e) { return e.alive; });
    bool none_dead = registry().none([](auto const& e) { return !e.alive; });

    std::cout << "  Has LibA threads: " << (has_libA ? "yes" : "no") << "\n";
    std::cout << "  All threads alive: " << (all_alive ? "yes" : "no") << "\n";
    std::cout << "  No dead threads: " << (none_dead ? "yes" : "no") << "\n";

    if (!has_libA || !all_alive || !none_dead)
    {
        std::cerr << "  ERROR: Predicate checks failed\n";
        success = false;
    }

    // Phase 3: Test find_if
    std::cout << "\nPhase 3: Testing find_if...\n";
    auto found = registry().find_if([](auto const& e) { return e.name == "inj-a1"; });
    if (!found)
    {
        std::cerr << "  ERROR: Could not find thread 'inj-a1'\n";
        success = false;
    }
    else
    {
        std::cout << "  Found thread: " << found->name << " (TID: " << found->tid << ")\n";
    }

    // Phase 4: Test map to extract TIDs
    std::cout << "\nPhase 4: Testing map to extract TIDs...\n";
    auto tids = registry().filter([](auto const& e) { return e.componentTag == "AppInjLibA"; }).map([](auto const& e) {
        return e.tid;
    });
    std::cout << "  LibA TIDs: ";
    for (auto tid : tids)
    {
        std::cout << tid << " ";
    }
    std::cout << "\n";

    if (tids.size() != 2)
    {
        std::cerr << "  ERROR: Expected 2 TIDs from LibA, got " << tids.size() << "\n";
        success = false;
    }

    // Phase 5: Test take/skip - pagination
    std::cout << "\nPhase 5: Testing take/skip...\n";
    auto first_two = registry().query().take(2).entries();
    std::cout << "  First 2 threads: ";
    for (auto const& e : first_two)
    {
        std::cout << e.name << " ";
    }
    std::cout << "\n";

    if (first_two.size() != 2)
    {
        std::cerr << "  ERROR: Expected 2 entries from take(2), got " << first_two.size() << "\n";
        success = false;
    }

    // Phase 6: Wait for all threads to finish
    std::cout << "\nPhase 6: Waiting for all threads to finish...\n";
    appinj_libA::wait_for_threads();
    appinj_libB::wait_for_threads();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int final_count = registry().count();
    std::cout << "  Final registry count: " << final_count << "\n";

    if (final_count != 0)
    {
        std::cerr << "  ERROR: Expected 0 threads at end, got " << final_count << "\n";
        success = false;
    }

    // Phase 7: Test empty check
    std::cout << "\nPhase 7: Testing empty check...\n";
    bool is_empty = registry().empty();
    if (!is_empty)
    {
        std::cerr << "  ERROR: Registry should be empty\n";
        success = false;
    }
    else
    {
        std::cout << "  Confirmed: Registry is empty\n";
    }

    // Clean up: reset registry in each DSO
    appinj_libA::set_registry(nullptr);
    appinj_libB::set_registry(nullptr);

    if (success)
    {
        std::cout << "\n=== App injection integration test PASSED (7/7 phases) ===\n";
        return 0;
    }
    else
    {
        std::cerr << "\n=== App injection integration test FAILED ===\n";
        return 1;
    }
}
