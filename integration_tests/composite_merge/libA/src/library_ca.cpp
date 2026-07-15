#include <chrono>
#include <library_ca/library_ca.hpp>
#include <memory>
#include <mutex>
#include <threadschedule/detail/thread_backend.hpp>
#include <threadschedule/registered_threads.hpp>
#include <threadschedule/thread_registry.hpp>
#include <vector>

using namespace threadschedule;

namespace composite_libA
{

static thread_registry_backend local_registry; // library-local
static std::mutex threads_mutex;
static std::vector<std::unique_ptr<detail::thread_backend>> threads;

void
start_worker(char const* name)
{
  std::lock_guard<std::mutex> lock(threads_mutex);
  threads.push_back(std::make_unique<detail::thread_backend>(
      [n = std::string(name)]()
        {
          auto_register_current_thread guard(local_registry, n,
                                             "CompositeLibA");
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }));
}

void
wait_for_threads()
{
  std::lock_guard<std::mutex> lock(threads_mutex);
  for (auto& t : threads)
    {
      if (t->joinable())
        t->join();
    }
  threads.clear();
}

auto
get_registry() -> thread_registry_backend&
{
  return local_registry;
}

} // namespace composite_libA
