#include <threadschedule/jthread.hpp>

#include <iostream>
#include <stop_token>

int
main()
{
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
  threadschedule::jthread worker(
      [](std::stop_token stop)
        {
          while (!stop.stop_requested())
            std::this_thread::yield();
        });

  if (!worker.request_stop())
    {
      std::cerr << "the worker was already asked to stop\n";
      return 1;
    }
  if (auto joined = worker.join(); !joined)
    {
      std::cerr << joined.error().message() << '\n';
      return 1;
    }
#else
  std::cerr << "std::jthread is unavailable in this C++20 library\n";
  return 1;
#endif
}
