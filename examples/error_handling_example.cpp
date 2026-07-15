#include <threadschedule/threadschedule.hpp>

#include <iostream>
#include <stdexcept>

int
main()
{
  threadschedule::thread_pool pool(2);

  auto submitted
      = pool.submit([]() -> int { throw std::runtime_error("task failed"); });
  if (!submitted)
    {
      std::cerr << "could not submit task: " << submitted.error().message()
                << '\n';
      return 1;
    }

  try
    {
      std::cout << submitted->get() << '\n';
    }
  catch (std::exception const& error)
    {
      // Submission errors use expected. Exceptions raised by the task stay
      // attached to its future and are observed here.
      std::cout << "task exception: " << error.what() << '\n';
    }
}
