#include <threadschedule/threadschedule.hpp>

#include <chrono>
#include <thread>

extern "C"
#ifdef _WIN32
    __declspec(dllexport)
#endif
    void
    libB_start()
{
  threadschedule::thread worker(
      []
        {
          auto& registry = threadschedule::global_registry();
          (void)registry.register_current_thread("rt-b1", "B");
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          (void)registry.unregister_current_thread();
        });
  (void)worker.detach();
}
