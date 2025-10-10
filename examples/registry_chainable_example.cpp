#include <chrono>
#include <iostream>
#include <thread>
#include <threadschedule/registered_threads.hpp>
#include <threadschedule/thread_registry.hpp>

using namespace threadschedule;

int main()
{
    std::cout << "=== Chainable Registry API Example ===" << std::endl;

    // Create registered threads with different tags
    ThreadWrapperReg io1("io-worker-1", "io", [] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

    ThreadWrapperReg io2("io-worker-2", "io", [] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

    ThreadWrapperReg compute1("compute-worker-1", "compute",
                              [] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

    ThreadWrapperReg compute2("compute-worker-2", "compute",
                              [] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Example 1: Count all threads (direct call without .query())
    auto total_count = registry().count();
    std::cout << "\n1. Total registered threads: " << total_count << std::endl;

    // Example 2: Count threads by tag (direct .filter() call)
    auto io_count = registry().filter([](RegisteredThreadInfo const& e) { return e.componentTag == "io"; }).count();
    std::cout << "2. IO threads: " << io_count << std::endl;

    auto compute_count =
        registry().filter([](RegisteredThreadInfo const& e) { return e.componentTag == "compute"; }).count();
    std::cout << "3. Compute threads: " << compute_count << std::endl;

    // Example 3: Chain multiple filters (direct .filter() call)
    auto io_worker_1_count =
        registry()
            .filter([](RegisteredThreadInfo const& e) { return e.componentTag == "io"; })
            .filter([](RegisteredThreadInfo const& e) { return e.name.find("worker-1") != std::string::npos; })
            .count();
    std::cout << "4. IO worker-1 threads: " << io_worker_1_count << std::endl;

    // Example 4: Rename all IO threads (direct .filter() call)
    std::cout << "\n5. Renaming all IO threads..." << std::endl;
    registry()
        .filter([](RegisteredThreadInfo const& e) { return e.componentTag == "io"; })
        .for_each([](RegisteredThreadInfo const& e) {
            auto result = registry().set_name(e.tid, "fast-" + e.name);
            if (result)
            {
                std::cout << "   Renamed thread " << e.tid << " to fast-" << e.name << std::endl;
            }
        });

    // Example 5: Set priority for compute threads (direct .filter() call)
    std::cout << "\n6. Setting priority for compute threads..." << std::endl;
    registry()
        .filter([](RegisteredThreadInfo const& e) { return e.componentTag == "compute"; })
        .for_each([](RegisteredThreadInfo const& e) {
            auto result = registry().set_priority(e.tid, ThreadPriority::highest());
            if (result)
            {
                std::cout << "   Set priority for thread " << e.tid << std::endl;
            }
        });

    // Example 6: List all threads with their details
    std::cout << "\n7. All registered threads:" << std::endl;
    registry().query().for_each([](RegisteredThreadInfo const& e) {
        std::cout << "   TID: " << e.tid << ", Name: " << e.name << ", Tag: " << e.componentTag
                  << ", Alive: " << (e.alive ? "yes" : "no") << std::endl;
    });

    // Example 7: Get entries as vector for custom processing (you can still use .query() explicitly if needed)
    auto entries = registry().query().entries();
    std::cout << "\n8. Custom processing of " << entries.size() << " entries:" << std::endl;
    for (auto const& e : entries)
    {
        if (e.componentTag == "io")
        {
            std::cout << "   Found IO thread: " << e.name << std::endl;
        }
    }

    // Example 8: Check if any/all/none threads match criteria
    std::cout << "\n9. Predicate checks:" << std::endl;
    bool has_io = registry().any([](RegisteredThreadInfo const& e) { return e.componentTag == "io"; });
    std::cout << "   Has IO threads: " << (has_io ? "yes" : "no") << std::endl;

    bool all_alive = registry().all([](RegisteredThreadInfo const& e) { return e.alive; });
    std::cout << "   All threads alive: " << (all_alive ? "yes" : "no") << std::endl;

    bool none_dead = registry().none([](RegisteredThreadInfo const& e) { return !e.alive; });
    std::cout << "   No dead threads: " << (none_dead ? "yes" : "no") << std::endl;

    // Example 9: Find specific thread
    std::cout << "\n10. Find specific thread:" << std::endl;
    auto found = registry().find_if([](RegisteredThreadInfo const& e) { return e.name == "io-worker-1"; });
    if (found)
    {
        std::cout << "   Found thread: " << found->name << " (TID: " << found->tid << ")" << std::endl;
    }

    // Example 10: Map - extract specific fields
    std::cout << "\n11. Map - extract TIDs:" << std::endl;
    auto tids = registry()
                    .filter([](RegisteredThreadInfo const& e) { return e.componentTag == "io"; })
                    .map([](RegisteredThreadInfo const& e) { return e.tid; });
    std::cout << "   IO thread TIDs: ";
    for (auto tid : tids)
    {
        std::cout << tid << " ";
    }
    std::cout << std::endl;

    // Example 11: Take/Skip - pagination
    std::cout << "\n12. Take/Skip - first 2 threads:" << std::endl;
    registry().query().take(2).for_each(
        [](RegisteredThreadInfo const& e) { std::cout << "   " << e.name << std::endl; });

    std::cout << "\n13. Skip first 2, take next 2:" << std::endl;
    registry().query().skip(2).take(2).for_each(
        [](RegisteredThreadInfo const& e) { std::cout << "   " << e.name << std::endl; });

    // Example 12: Empty check
    std::cout << "\n14. Empty check:" << std::endl;
    bool no_gpu_threads =
        registry().filter([](RegisteredThreadInfo const& e) { return e.componentTag == "gpu"; }).empty();
    std::cout << "   No GPU threads: " << (no_gpu_threads ? "yes" : "no") << std::endl;

    io1.join();
    io2.join();
    compute1.join();
    compute2.join();

    std::cout << "\n=== Example completed ===" << std::endl;
    return 0;
}
