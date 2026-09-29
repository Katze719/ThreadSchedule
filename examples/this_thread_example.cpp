#include <threadschedule/this_thread.hpp>

#include <iostream>
#include <system_error>
#include <thread>

namespace ts = threadschedule;

int
main()
{
  std::error_code configuration_error;
  int answer = 0;

  std::thread worker(
      [&]
        {
          if (auto configured = ts::this_thread::set_priority(ts::priority_level::low); !configured)
            {
              configuration_error = configured.error();
              return;
            }
          answer = 42; // Short-lived work, after successful configuration.
        });

  worker.join();

  if (configuration_error)
    {
      std::cerr << "Could not configure the thread: " << configuration_error.message() << '\n';
      return 1;
    }
  std::cout << "answer: " << answer << '\n';
}
