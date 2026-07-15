#include <threadschedule/threadschedule.hpp>

#include <chrono>
#include <thread>
#include <vector>

int
main()
{
  threadschedule::thread_registry first;
  threadschedule::thread_registry second;

  threadschedule::thread first_worker(
      [&first]
        {
          (void)first.register_current_thread("first", "A");
          std::this_thread::sleep_for(std::chrono::milliseconds(80));
          (void)first.unregister_current_thread();
        });
  threadschedule::thread second_worker(
      [&second]
        {
          (void)second.register_current_thread("second", "B");
          std::this_thread::sleep_for(std::chrono::milliseconds(80));
          (void)second.unregister_current_thread();
        });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  auto first_entries = first.snapshot();
  auto second_entries = second.snapshot();
  if (!first_entries || !second_entries)
    return 3;

  std::vector<threadschedule::registered_thread> merged
      = std::move(*first_entries);
  merged.insert(merged.end(), second_entries->begin(), second_entries->end());

  auto first_joined = first_worker.join();
  auto second_joined = second_worker.join();
  return merged.size() == 2 && first_joined && second_joined ? 0 : 4;
}
