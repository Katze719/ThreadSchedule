#include <threadschedule/threadschedule.hpp>

#include <iostream>

int
main()
{
  threadschedule::thread_pool_config config;
  config.worker_count = 4;
  config.workers.name = "worker";
  config.workers.scheduling = threadschedule::schedule::normal();

  threadschedule::thread_pool pool(std::move(config));

  auto answer = pool.submit([] { return 42; });
  if (!answer)
    {
      std::cerr << "submission failed: " << answer.error().message() << '\n';
      return 1;
    }

  std::cout << "answer: " << answer->get() << '\n';
}
