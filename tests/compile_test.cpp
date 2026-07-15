#include <threadschedule/threadschedule.hpp>

#include <iostream>

int
main()
{
  threadschedule::thread_affinity affinity({ 0 });
  threadschedule::thread_config config;
  config.scheduling = threadschedule::schedule::normal();
  config.affinity = affinity;

  threadschedule::thread_registry registry;
  threadschedule::thread_pool pool(1);

  std::cout << "ThreadSchedule v3 core compilation test passed\n";
  return 0;
}
