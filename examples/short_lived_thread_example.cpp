#include <threadschedule/thread_view.hpp>

#include <future>
#include <iostream>
#include <thread>

namespace ts = threadschedule;

int
main()
{
  std::promise<void> start;
  auto ready = start.get_future();
  int answer = 0;

  std::thread worker(
      [&]
        {
          ready.wait(); // Keep the thread alive until configuration is complete.
          answer = 42;  // Short-lived work.
        });

  ts::thread_view view(worker);
  auto named = view.set_name("short-worker");

  // Release and join even if naming failed; the work can still run.
  start.set_value();
  worker.join();

  if (!named)
    {
      std::cerr << "Could not name the thread: " << named.error().message() << '\n';
      return 1;
    }
  std::cout << "answer: " << answer << '\n';
}
