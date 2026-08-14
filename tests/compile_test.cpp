#include <threadschedule/threadschedule.hpp>

#include <iostream>

int
main()
{
  threadschedule::thread_affinity affinity({ threadschedule::cpu_id{ 0 } });
  threadschedule::thread_config config;
  config.set_scheduling(threadschedule::schedule::priority(threadschedule::priority_level::low)).set_affinity(affinity);

  threadschedule::thread_registry registry;
  threadschedule::thread_pool pool(threadschedule::worker_count{ 1 });

  std::cout << "ThreadSchedule v3 core compilation test passed\n";
  return 0;
}
