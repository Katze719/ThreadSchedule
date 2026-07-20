#include <threadschedule/threadschedule.hpp>

#include <chrono>
#include <iostream>
#include <thread>

int
main()
{
  threadschedule::thread worker(
      []
        {
          auto& registry = threadschedule::global_registry();
          (void)registry.register_current_thread("worker-1", "io");
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          (void)registry.unregister_current_thread();
        });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  auto entries = threadschedule::global_registry().snapshot();
  if (!entries)
    return 2;

  for (auto const& entry : *entries)
    std::cout << entry.name << " [" << entry.component << "]\n";

  return worker.join() ? 0 : 3;
}
