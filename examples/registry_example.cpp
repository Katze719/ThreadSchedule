#include <chrono>
#include <threadschedule/registered_threads.hpp>
#include <threadschedule/thread_registry.hpp>

using namespace threadschedule;

int main()
{
    // Create a registered worker thread with tags
    ThreadWrapperReg worker("worker-1", "io", [] {
        // Simulate some work
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    });

    // Wait a moment to ensure the thread has started and registered
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Apply a no-op priority change to all io threads (demo)
    registry().apply([](RegisteredThreadInfo const& e) { return e.componentTag == "io"; },
                     [](RegisteredThreadInfo const& e) { (void)registry().set_priority(e.tid, ThreadPriority{0}); });

    // Try to rename all io threads
    registry().apply(
        [](RegisteredThreadInfo const& e) { return e.componentTag == "io"; },
        [](RegisteredThreadInfo const& e) { (void)registry().set_name(e.tid, std::string("io-") + e.name); });

    worker.join();
    return 0;
}
