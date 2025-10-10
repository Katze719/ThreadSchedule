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
    bool success = true;

    // Phase 1: Start 4 threads
    std::cout << "\nPhase 1: Starting 4 threads (2 per library)...\n";
    composite_libA::start_worker("ca-1");
    composite_libA::start_worker("ca-2");
    composite_libB::start_worker("cb-1");
    composite_libB::start_worker("cb-2");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Phase 2: Verify individual library registries
    std::cout << "\nPhase 2: Verifying individual registries...\n";
    size_t a_count = composite_libA::get_registry().count();
    size_t b_count = composite_libB::get_registry().count();

    std::cout << "  LibA registry: " << a_count << " threads\n";
    std::cout << "  LibB registry: " << b_count << " threads\n";

    if (a_count != 2)
    {
        std::cerr << "  ERROR: Expected 2 threads in LibA registry, got " << a_count << "\n";
        success = false;
    }
    if (b_count != 2)
    {
        std::cerr << "  ERROR: Expected 2 threads in LibB registry, got " << b_count << "\n";
        success = false;
    }

    // Phase 3: Verify tags in individual registries
    std::cout << "\nPhase 3: Verifying component tags...\n";
    bool libA_tags_ok =
        composite_libA::get_registry().all([](auto const& e) { return e.componentTag == "CompositeLibA"; });
    bool libB_tags_ok =
        composite_libB::get_registry().all([](auto const& e) { return e.componentTag == "CompositeLibB"; });

    if (!libA_tags_ok)
    {
        std::cerr << "  ERROR: LibA has threads with wrong tags\n";
        success = false;
    }
    if (!libB_tags_ok)
    {
        std::cerr << "  ERROR: LibB has threads with wrong tags\n";
        success = false;
    }
    std::cout << "  Component tags verified\n";

    // Phase 4: Create composite and verify merge
    std::cout << "\nPhase 4: Creating composite registry...\n";
    CompositeThreadRegistry comp;
    comp.attach(&composite_libA::get_registry());
    comp.attach(&composite_libB::get_registry());

    size_t total = comp.count();
    std::cout << "  Composite registry: " << total << " threads\n";

    if (total != 4)
    {
        std::cerr << "  ERROR: Expected 4 threads in composite registry, got " << total << "\n";
        success = false;
    }

    // Phase 5: Test composite query operations
    std::cout << "\nPhase 5: Testing composite query operations...\n";
    size_t comp_a = comp.filter([](auto const& e) { return e.componentTag == "CompositeLibA"; }).count();
    size_t comp_b = comp.filter([](auto const& e) { return e.componentTag == "CompositeLibB"; }).count();

    std::cout << "  Composite: LibA=" << comp_a << ", LibB=" << comp_b << "\n";

    if (comp_a != 2 || comp_b != 2)
    {
        std::cerr << "  ERROR: Composite filter counts incorrect\n";
        success = false;
    }

    // Phase 6: Test find_if in composite
    std::cout << "\nPhase 6: Testing find_if in composite...\n";
    auto found = comp.find_if([](auto const& e) { return e.name == "ca-1"; });
    if (!found)
    {
        std::cerr << "  ERROR: Could not find thread 'ca-1' in composite\n";
        success = false;
    }
    else
    {
        std::cout << "  Found thread: " << found->name << " from " << found->componentTag << "\n";
    }

    // Phase 7: Test take/skip
    std::cout << "\nPhase 7: Testing take/skip...\n";
    auto first_two = comp.query().take(2).entries();
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

    // Phase 8: Wait for all threads and verify empty
    std::cout << "\nPhase 8: Waiting for all threads to finish...\n";
    composite_libA::wait_for_threads();
    composite_libB::wait_for_threads();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    size_t final_total = comp.count();
    bool comp_empty = comp.empty();
    bool libA_empty = composite_libA::get_registry().empty();
    bool libB_empty = composite_libB::get_registry().empty();

    std::cout << "  Final counts: composite=" << final_total << ", A=" << (libA_empty ? "empty" : "not empty")
              << ", B=" << (libB_empty ? "empty" : "not empty") << "\n";

    if (!comp_empty || !libA_empty || !libB_empty)
    {
        std::cerr << "  ERROR: All registries should be empty\n";
        success = false;
    }

    if (success)
    {
        std::cout << "\n=== Composite merge scenario PASSED (8/8 phases) ===\n";
        return 0;
    }
    else
    {
        std::cerr << "\n=== Composite merge scenario FAILED ===\n";
        return 1;
    }
}
