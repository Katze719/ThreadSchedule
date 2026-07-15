#include <library_ra/library_ra.hpp>

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <threadschedule/threadschedule.hpp>
#include <vector>

namespace runtime_libA
{
namespace
{
std::mutex threads_mutex;
std::vector<threadschedule::thread> threads;
} // namespace

void
start_worker(char const* name)
{
  std::lock_guard<std::mutex> lock(threads_mutex);
  threads.emplace_back(
      [thread_name = std::string(name)]
        {
          auto& registry = threadschedule::global_registry();
          (void)registry.register_current_thread(thread_name, "RuntimeLibA");
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
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

} // namespace runtime_libA
