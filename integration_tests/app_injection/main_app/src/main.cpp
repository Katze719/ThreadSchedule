#include <appinj_libA/libA.hpp>
#include <appinj_libB/libB.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <threadschedule/threadschedule.hpp>

int
main()
{
  threadschedule::thread_registry registry;
  threadschedule::use_global_registry(&registry);
  appinj_libA::set_registry(&registry);
  appinj_libB::set_registry(&registry);

  appinj_libA::start_worker("inj-a1");
  appinj_libA::start_worker("inj-a2");
  appinj_libB::start_worker("inj-b1");
  appinj_libB::start_worker("inj-b2");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto snapshot = registry.snapshot();
  if (!snapshot)
    return 1;

  auto const count_component = [&snapshot](char const* component)
    {
      return std::count_if(snapshot->begin(), snapshot->end(),
                           [component](auto const& entry)
                             { return entry.component == component; });
    };
  auto const all_alive
      = std::all_of(snapshot->begin(), snapshot->end(),
                    [](auto const& entry) { return entry.alive; });
  auto const found
      = std::find_if(snapshot->begin(), snapshot->end(),
                     [](auto const& entry) { return entry.name == "inj-a1"; });

  bool const valid = snapshot->size() == 4
                     && count_component("AppInjLibA") == 2
                     && count_component("AppInjLibB") == 2 && all_alive
                     && found != snapshot->end();

  appinj_libA::wait_for_threads();
  appinj_libB::wait_for_threads();
  bool const empty = registry.empty();

  appinj_libA::set_registry(nullptr);
  appinj_libB::set_registry(nullptr);
  threadschedule::use_global_registry(nullptr);

  std::cout << "registered=" << snapshot->size() << ", empty=" << empty
            << '\n';
  return valid && empty ? 0 : 2;
}
