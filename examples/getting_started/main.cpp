#include <threadschedule/threadschedule.hpp>

#include <iostream>

int
main()
{
  threadschedule::thread_pool pool(threadschedule::worker_count{ 2 });
  auto answer = pool.submit([] { return 42; });
  if (!answer)
    {
      std::cerr << "submission failed: " << answer.error().message() << '\n';
      return 1;
    }

  std::cout << "answer: " << answer->get() << '\n';
}
