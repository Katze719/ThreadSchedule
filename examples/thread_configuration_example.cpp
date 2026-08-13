#include <threadschedule/threadschedule.hpp>

#include <iostream>

int
main()
{
  auto allowed = threadschedule::this_thread::get_affinity();
  if (!allowed)
    {
      std::cerr << "Could not read the allowed CPUs: " << allowed.error().message() << '\n';
      return 1;
    }
  if (allowed->empty())
    {
      std::cerr << "The calling thread has no allowed CPUs\n";
      return 1;
    }

  threadschedule::thread_config config;
  config.name = "metrics";
  config.scheduling = threadschedule::schedule::priority(threadschedule::priority_level::low);
  config.affinity = threadschedule::thread_affinity({ allowed->cpus().front() });

  if (auto worker = threadschedule::thread::create(config,
                                                   []
                                                     {
                                                       // Collect metrics on
                                                       // the configured
                                                       // thread.
                                                     });
      !worker)
    {
      std::cerr << "Could not configure the thread: " << worker.error().message() << '\n';
      return 1;
    }
  else if (auto join_result = worker->join(); !join_result)
    {
      std::cerr << "Could not join the thread: " << join_result.error().message() << '\n';
      return 1;
    }
}
