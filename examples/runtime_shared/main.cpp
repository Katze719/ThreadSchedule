#include <threadschedule/threadschedule.hpp>

#include <chrono>
#include <iostream>
#include <thread>

extern "C" void lib_a_start();
extern "C" void lib_b_start();

int
main()
{
  lib_a_start();
  lib_b_start();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));

  auto entries = threadschedule::global_registry().snapshot();
  if (!entries)
    return 1;

  for (auto const& entry : *entries)
    std::cout << "thread: " << entry.name << " tag=" << entry.component << '\n';
  std::cout << "total=" << entries->size() << '\n';
  return entries->size() == 2 ? 0 : 2;
}
