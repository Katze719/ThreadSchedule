#include <library_ra/library_ra.hpp>
#include <library_rb/library_rb.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <threadschedule/threadschedule.hpp>

int
main()
{
  runtime_libA::start_worker("ra-1");
  runtime_libA::start_worker("ra-2");
  runtime_libB::start_worker("rb-1");
  runtime_libB::start_worker("rb-2");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  auto& registry = threadschedule::global_registry();
  auto snapshot = registry.snapshot();
  if (!snapshot)
    return 1;

  auto const count_component = [&snapshot](char const* component)
    {
      return std::count_if(snapshot->begin(), snapshot->end(),
                           [component](auto const& entry)
                             { return entry.component == component; });
    };
  auto const has_name = [&snapshot](char const* name)
    {
      return std::any_of(snapshot->begin(), snapshot->end(),
                         [name](auto const& entry)
                           { return entry.name == name; });
    };
  auto const all_alive
      = std::all_of(snapshot->begin(), snapshot->end(),
                    [](auto const& entry) { return entry.alive; });
  bool const valid = snapshot->size() == 4
                     && count_component("RuntimeLibA") == 2
                     && count_component("RuntimeLibB") == 2 && has_name("ra-1")
                     && has_name("rb-1") && all_alive;

  runtime_libA::wait_for_threads();
  runtime_libB::wait_for_threads();
  bool const empty = registry.empty();

  std::cout << "registered=" << snapshot->size() << ", empty=" << empty
            << '\n';
  return valid && empty ? 0 : 2;
}
