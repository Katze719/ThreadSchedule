#include <threadschedule/thread_pool.hpp>

#include <iostream>

int
main()
{
  threadschedule::thread_pool_config config;
  threadschedule::thread_config workers;
  workers.set_name("worker").set_scheduling(threadschedule::schedule::normal());
  config.set_worker_count(threadschedule::worker_count{ 4 }).set_worker_config(std::move(workers));

  threadschedule::thread_pool pool(std::move(config));

  auto answer = pool.submit([] { return 42; });
  if (!answer)
    {
      std::cerr << "submission failed: " << answer.error().message() << '\n';
      return 1;
    }

  std::cout << "answer: " << answer->get() << '\n';
}
