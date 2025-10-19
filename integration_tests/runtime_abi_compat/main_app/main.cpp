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
    std::cout << "\n=== Runtime ABI Compatibility Test ===\n";
    bool success = true;

    // Phase 1: Start 4 threads from two DSOs (A=new, B=old)
    std::cout << "\nPhase 1: Starting 4 threads (LibA=new, LibB=old) ...\n";
    runtime_libA::start_worker("ra-1");
    runtime_libA::start_worker("ra-2");
    runtime_libB::start_worker("rb-1");
    runtime_libB::start_worker("rb-2");

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Phase 2: Verify shared registry sees all 4 threads
    std::cout << "\nPhase 2: Verifying shared runtime registry...\n";
    size_t total = registry().count();
    size_t a = registry()
                   .filter([](auto const& e) { return e.componentTag.find("RuntimeAbiLibA") != std::string::npos; })
                   .count();
    size_t b = registry()
                   .filter([](auto const& e) { return e.componentTag.find("RuntimeAbiLibB-OLD") != std::string::npos; })
                   .count();

    std::cout << "  Total threads in runtime registry: " << total << "\n";
    std::cout << "  LibA(new): " << a << ", LibB(old): " << b << "\n";

    if (total != 4)
    {
        std::cerr << "  ERROR: Expected 4 total threads, got " << total << "\n";
        success = false;
    }
    if (a != 2)
    {
        std::cerr << "  ERROR: Expected 2 threads from LibA(new), got " << a << "\n";
        success = false;
    }
    if (b != 2)
    {
        std::cerr << "  ERROR: Expected 2 threads from LibB(old), got " << b << "\n";
        success = false;
    }

    // Phase 3: Basic predicates across mixed versions
    std::cout << "\nPhase 3: Testing query predicates...\n";
    bool has_libA =
        registry().any([](auto const& e) { return e.componentTag.find("RuntimeAbiLibA") != std::string::npos; });
    bool has_libB =
        registry().any([](auto const& e) { return e.componentTag.find("RuntimeAbiLibB-OLD") != std::string::npos; });
    bool all_alive = registry().all([](auto const& e) { return e.alive; });

    std::cout << "  Has LibA(new): " << (has_libA ? "yes" : "no") << "\n";
    std::cout << "  Has LibB(old): " << (has_libB ? "yes" : "no") << "\n";
    std::cout << "  All alive: " << (all_alive ? "yes" : "no") << "\n";

    if (!has_libA || !has_libB || !all_alive)
    {
        std::cerr << "  ERROR: Predicate checks failed (possible ABI/runtime mismatch)\n";
        success = false;
    }

    // Phase 4: Wait for all threads and verify registry is empty
    std::cout << "\nPhase 4: Waiting for all threads to finish...\n";
    runtime_libA::wait_for_threads();
    runtime_libB::wait_for_threads();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    size_t final_count = registry().count();
    bool is_empty = registry().empty();

    std::cout << "  Final count: " << final_count << ", Empty: " << (is_empty ? "yes" : "no") << "\n";

    if (final_count != 0 || !is_empty)
    {
        std::cerr << "  ERROR: Registry should be empty at end\n";
        success = false;
    }

    if (success)
    {
        std::cout << "\n=== Runtime ABI compatibility test PASSED ===\n";
        return 0;
    }
    else
    {
        std::cerr << "\n=== Runtime ABI compatibility test FAILED ===\n";
        return 1;
    }
}
