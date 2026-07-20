#include <library_ca/library_ca.hpp>
#include <library_cb/library_cb.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int
main()
{
  composite_libA::start_worker("ca-1");
  composite_libA::start_worker("ca-2");
  composite_libB::start_worker("cb-1");
  composite_libB::start_worker("cb-2");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto first = composite_libA::get_registry().snapshot();
  auto second = composite_libB::get_registry().snapshot();
  if (!first || !second)
    return 1;

  std::vector<threadschedule::registered_thread> merged = std::move(*first);
  merged.insert(merged.end(), second->begin(), second->end());

  auto const component_count = [&merged](char const* component)
    {
      return std::count_if(merged.begin(), merged.end(),
                           [component](auto const& entry)
                             { return entry.component == component; });
    };
  auto const found
      = std::find_if(merged.begin(), merged.end(),
                     [](auto const& entry) { return entry.name == "ca-1"; });
  bool const valid
      = merged.size() == 4 && component_count("CompositeLibA") == 2
        && component_count("CompositeLibB") == 2 && found != merged.end();

  composite_libA::wait_for_threads();
  composite_libB::wait_for_threads();
  bool const empty = composite_libA::get_registry().empty()
                     && composite_libB::get_registry().empty();

  std::cout << "merged=" << merged.size() << ", empty=" << empty << '\n';
  return valid && empty ? 0 : 2;
}
