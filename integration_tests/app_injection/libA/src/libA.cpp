#include <appinj_libA/libA.hpp>

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace appinj_libA
{
namespace
{
std::mutex threads_mutex;
std::vector<threadschedule::thread> threads;
} // namespace

void
set_registry(threadschedule::thread_registry* registry)
{
  threadschedule::use_global_registry(registry);
}

void
start_worker(char const* name)
{
  std::lock_guard<std::mutex> lock(threads_mutex);
  threads.emplace_back(
      [thread_name = std::string(name)]
        {
          auto& registry = threadschedule::global_registry();
          (void)registry.register_current_thread(thread_name, "AppInjLibA");
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          (void)registry.unregister_current_thread();
        });
}

void
wait_for_threads()
{
  std::lock_guard<std::mutex> lock(threads_mutex);
  for (auto& worker : threads)
    (void)worker.join();
  threads.clear();
}

} // namespace appinj_libA
