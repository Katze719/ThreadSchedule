#include <threadschedule/scheduled_pool.hpp>

#include <chrono>
#include <future>
#include <iostream>

using namespace std::chrono_literals;

int
main()
{
  threadschedule::scheduled_pool scheduler(threadschedule::worker_count{ 2 });

  std::promise<void> finished;
  auto done = finished.get_future();
  auto task = scheduler.schedule_after(25ms, [&finished] { finished.set_value(); });
  if (!task)
    {
      std::cerr << task.error().message() << '\n';
      return 1;
    }

  done.wait();
  scheduler.shutdown();
}
