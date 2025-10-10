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
    bool success = true;

    // Phase 1: Start 4 threads from two DSOs
    std::cout << "\nPhase 1: Starting 4 threads from 2 DSOs...\n";
    runtime_libA::start_worker("ra-1");
    runtime_libA::start_worker("ra-2");
    runtime_libB::start_worker("rb-1");
    runtime_libB::start_worker("rb-2");

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Phase 2: Verify shared registry sees all 4 threads
    std::cout << "\nPhase 2: Verifying shared runtime registry...\n";
    int total = registry().count();
    int a = registry().filter([](auto const& e) { return e.componentTag == "RuntimeLibA"; }).count();
    int b = registry().filter([](auto const& e) { return e.componentTag == "RuntimeLibB"; }).count();

    std::cout << "  Total threads in runtime registry: " << total << "\n";
    std::cout << "  RuntimeLibA: " << a << ", RuntimeLibB: " << b << "\n";

    if (total != 4)
    {
        std::cerr << "  ERROR: Expected 4 total threads, got " << total << "\n";
        success = false;
    }
    if (a != 2)
    {
        std::cerr << "  ERROR: Expected 2 threads from RuntimeLibA, got " << a << "\n";
        success = false;
    }
    if (b != 2)
    {
        std::cerr << "  ERROR: Expected 2 threads from RuntimeLibB, got " << b << "\n";
        success = false;
    }

    // Phase 3: Test query predicates on shared registry
    std::cout << "\nPhase 3: Testing query predicates...\n";
    bool has_libA = registry().any([](auto const& e) { return e.componentTag == "RuntimeLibA"; });
    bool has_libB = registry().any([](auto const& e) { return e.componentTag == "RuntimeLibB"; });
    bool all_alive = registry().all([](auto const& e) { return e.alive; });

    std::cout << "  Has RuntimeLibA: " << (has_libA ? "yes" : "no") << "\n";
    std::cout << "  Has RuntimeLibB: " << (has_libB ? "yes" : "no") << "\n";
    std::cout << "  All alive: " << (all_alive ? "yes" : "no") << "\n";

    if (!has_libA || !has_libB || !all_alive)
    {
        std::cerr << "  ERROR: Predicate checks failed\n";
        success = false;
    }

    // Phase 4: Test find_if across DSOs
    std::cout << "\nPhase 4: Testing find_if across DSOs...\n";
    auto found_a = registry().find_if([](auto const& e) { return e.name == "ra-1"; });
    auto found_b = registry().find_if([](auto const& e) { return e.name == "rb-1"; });

    if (!found_a)
    {
        std::cerr << "  ERROR: Could not find thread 'ra-1' from LibA\n";
        success = false;
    }
    else
    {
        std::cout << "  Found LibA thread: " << found_a->name << " (TID: " << found_a->tid << ")\n";
    }

    if (!found_b)
    {
        std::cerr << "  ERROR: Could not find thread 'rb-1' from LibB\n";
        success = false;
    }
    else
    {
        std::cout << "  Found LibB thread: " << found_b->name << " (TID: " << found_b->tid << ")\n";
    }

    // Phase 5: Test map to extract TIDs from both DSOs
    std::cout << "\nPhase 5: Testing map across DSOs...\n";
    auto libA_tids =
        registry().filter([](auto const& e) { return e.componentTag == "RuntimeLibA"; }).map([](auto const& e) {
            return e.tid;
        });
    auto libB_tids =
        registry().filter([](auto const& e) { return e.componentTag == "RuntimeLibB"; }).map([](auto const& e) {
            return e.tid;
        });

    std::cout << "  LibA TIDs: ";
    for (auto tid : libA_tids)
        std::cout << tid << " ";
    std::cout << "\n  LibB TIDs: ";
    for (auto tid : libB_tids)
        std::cout << tid << " ";
    std::cout << "\n";

    if (libA_tids.size() != 2 || libB_tids.size() != 2)
    {
        std::cerr << "  ERROR: Unexpected TID counts\n";
        success = false;
    }

    // Phase 6: Test take/skip
    std::cout << "\nPhase 6: Testing take/skip...\n";
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

    // Phase 7: Wait for all threads and verify registry is empty
    std::cout << "\nPhase 7: Waiting for all threads to finish...\n";
    runtime_libA::wait_for_threads();
    runtime_libB::wait_for_threads();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int final_count = registry().count();
    bool is_empty = registry().empty();

    std::cout << "  Final count: " << final_count << ", Empty: " << (is_empty ? "yes" : "no") << "\n";

    if (final_count != 0 || !is_empty)
    {
        std::cerr << "  ERROR: Registry should be empty at end\n";
        success = false;
    }

    if (success)
    {
        std::cout << "\n=== Runtime single registry test PASSED (7/7 phases) ===\n";
        return 0;
    }
    else
    {
        std::cerr << "\n=== Runtime single registry test FAILED ===\n";
        return 1;
    }
}
