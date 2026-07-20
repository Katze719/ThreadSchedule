#include <threadschedule/threadschedule.hpp>

#include <iostream>

int
main()
{
  threadschedule::thread_config config;
  config.name = "metrics";
  config.scheduling = threadschedule::schedule::priority(
      threadschedule::priority_level::low);
  config.affinity = threadschedule::thread_affinity({ 0 });

  if (auto worker = threadschedule::thread::create(config,
                                                   []
                                                     {
                                                       // Collect metrics on
                                                       // the configured
                                                       // thread.
                                                     });
      !worker)
    {
      std::cerr << "Could not configure the thread: "
                << worker.error().message() << '\n';
      return 1;
    }
  else if (auto join_result = worker->join(); !join_result)
    {
      std::cerr << "Could not join the thread: "
                << join_result.error().message() << '\n';
      return 1;
    }
}
