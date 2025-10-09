#include <appinj_libA/libA.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <threadschedule/registered_threads.hpp>
#include <threadschedule/thread_registry.hpp>
#include <threadschedule/thread_wrapper.hpp>
#include <vector>

using namespace threadschedule;

namespace appinj_libA
{

static std::mutex threads_mutex;
static std::vector<std::unique_ptr<ThreadWrapper>> threads;

// Allow the app to inject its registry into this DSO
void set_registry(ThreadRegistry* reg)
{
    set_external_registry(reg);
}

void start_worker(char const* name)
{
    std::lock_guard<std::mutex> lock(threads_mutex);
    threads.push_back(std::make_unique<ThreadWrapper>([n = std::string(name)]() {
        AutoRegisterCurrentThread guard(n, "AppInjLibA");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }));
}

void wait_for_threads()
{
    std::lock_guard<std::mutex> lock(threads_mutex);
    for (auto& t : threads)
    {
        if (t->joinable())
            t->join();
    }
    threads.clear();
}

} // namespace appinj_libA
