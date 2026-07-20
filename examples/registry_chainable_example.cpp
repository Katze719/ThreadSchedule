#include <threadschedule/threadschedule.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

int
main()
{
  threadschedule::thread io_worker(
      []
        {
          auto& registry = threadschedule::global_registry();
          (void)registry.register_current_thread("io-worker", "io");
          std::this_thread::sleep_for(std::chrono::milliseconds(80));
          (void)registry.unregister_current_thread();
        });
  threadschedule::thread compute_worker(
      []
        {
          auto& registry = threadschedule::global_registry();
          (void)registry.register_current_thread("compute-worker", "compute");
          std::this_thread::sleep_for(std::chrono::milliseconds(80));
          (void)registry.unregister_current_thread();
        });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  auto entries = threadschedule::global_registry().snapshot();
  if (!entries)
    return 2;

  auto const io_count
      = std::count_if(entries->begin(), entries->end(),
                      [](threadschedule::registered_thread const& entry)
                        { return entry.component == "io"; });
  std::cout << "registered=" << entries->size() << ", io=" << io_count << '\n';

  auto io_joined = io_worker.join();
  auto compute_joined = compute_worker.join();
  return io_count == 1 && io_joined && compute_joined ? 0 : 3;
}
