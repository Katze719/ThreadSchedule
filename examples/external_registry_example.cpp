#include <threadschedule/threadschedule.hpp>

#include <chrono>
#include <thread>

int
main()
{
  threadschedule::thread_registry registry;
  threadschedule::use_global_registry(&registry);

  threadschedule::thread worker(
      []
        {
          auto& global = threadschedule::global_registry();
          (void)global.register_current_thread("external-worker",
                                               "application");
          std::this_thread::sleep_for(std::chrono::milliseconds(60));
          (void)global.unregister_current_thread();
        });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  auto entries = registry.snapshot();
  auto joined = worker.join();
  threadschedule::use_global_registry(nullptr);
  return entries && entries->size() == 1 && joined ? 0 : 3;
}
