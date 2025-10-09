#include "library_a/library_a.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <threadschedule/registered_threads.hpp>
#include <threadschedule/thread_wrapper.hpp>
#include <vector>

using namespace threadschedule;

namespace library_a
{

// Static registry for library A
static ThreadRegistry local_registry;
static ThreadRegistry* active_registry = &local_registry;
static std::mutex registry_mutex;

// Thread management
static std::mutex threads_mutex;
static std::vector<std::unique_ptr<ThreadWrapper>> threads;

ThreadRegistry& get_registry()
{
    std::lock_guard<std::mutex> lock(registry_mutex);
    return *active_registry;
}

void set_registry(ThreadRegistry* reg)
{
    std::lock_guard<std::mutex> lock(registry_mutex);
    if (reg != nullptr)
    {
        active_registry = reg;
        // Also set it globally for this library
        set_external_registry(reg);
    }
    else
    {
        active_registry = &local_registry;
        set_external_registry(nullptr);
    }
}

void start_worker(char const* name)
{
    std::lock_guard<std::mutex> lock(threads_mutex);

    threads.push_back(std::make_unique<ThreadWrapper>([name]() {
        // Get the active registry for this library
        ThreadRegistry* reg;
        {
            std::lock_guard<std::mutex> lock(registry_mutex);
            reg = active_registry;
        }

        // Register thread in the active registry
        AutoRegisterCurrentThread guard(*reg, name, "LibraryA");

        // Simulate some work
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }));
}

void wait_for_threads()
{
    std::lock_guard<std::mutex> lock(threads_mutex);
    for (auto& thread : threads)
    {
        if (thread->joinable())
        {
            thread->join();
        }
    }
    threads.clear();
}

int get_thread_count()
{
    ThreadRegistry& reg = get_registry();
    int count = 0;
    reg.for_each([&count](RegisteredThreadInfo const&) { count++; });
    return count;
}

} // namespace library_a
