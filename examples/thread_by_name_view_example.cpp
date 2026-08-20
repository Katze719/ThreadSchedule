#include <threadschedule/advanced/thread_by_name_view.hpp>
#include <threadschedule/thread_view.hpp>

#include <future>
#include <iostream>
#include <thread>
#include <utility>

int
main()
{
  std::promise<void> release;
  auto released = release.get_future();
  std::thread worker([released = std::move(released)]() mutable { released.wait(); });
  auto finish = [&]
    {
      release.set_value();
      worker.join();
    };

  threadschedule::thread_view worker_view(worker);
  if (auto named = worker_view.set_name("lookup-worker"); !named)
    {
      std::cerr << "Could not name worker: " << named.error().message() << '\n';
      finish();
      return 1;
    }

  auto found = threadschedule::advanced::thread_by_name_view::create("lookup-worker");
  if (!found)
    {
      std::cerr << "Could not find worker: " << found.error().message() << '\n';
      finish();
      return 1;
    }

  auto matches = threadschedule::advanced::thread_by_name_view::find_all("lookup-worker");
  if (!matches)
    {
      std::cerr << "Could not enumerate workers: " << matches.error().message() << '\n';
      finish();
      return 1;
    }

  std::cout << "Found " << matches->size() << " worker with native ID " << found->native_id() << '\n';
  finish();
}
