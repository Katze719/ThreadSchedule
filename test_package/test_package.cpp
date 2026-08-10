#include <threadschedule/threadschedule.hpp>

#include <iostream>

int
main()
{
  threadschedule::thread_pool pool(1);
  auto result = pool.submit([] { return 42; });
  if (!result)
    {
      std::cerr << result.error().message() << '\n';
      return 1;
    }
  return result->get() == 42 ? 0 : 1;
}
