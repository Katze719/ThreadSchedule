#include <library_ca/library_ca.hpp>

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace composite_libA
{
namespace
{
threadschedule::thread_registry local_registry;
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
          (void)local_registry.register_current_thread(thread_name,
                                                       "CompositeLibA");
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          (void)local_registry.unregister_current_thread();
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

auto
get_registry() -> threadschedule::thread_registry&
{
  return local_registry;
}

} // namespace composite_libA
