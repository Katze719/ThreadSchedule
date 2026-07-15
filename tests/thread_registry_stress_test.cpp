#include <atomic>
#include <gtest/gtest.h>
#include <threadschedule/detail/registered_thread_backend.hpp>
#include <threadschedule/thread_registry.hpp>

using namespace threadschedule;

TEST(ThreadRegistryStress, ManyThreadsRegisterAndControl)
{
  constexpr int kThreads = 32;
  std::vector<std::unique_ptr<detail::registered_thread_backend>> threads;
  threads.reserve(kThreads);
  std::atomic<int> ran{ 0 };

  for (int i = 0; i < kThreads; ++i)
    {
      threads.emplace_back(std::make_unique<detail::registered_thread_backend>(
          std::string("w-") + std::to_string(i), (i % 2 == 0) ? "even" : "odd",
          [&]
            {
              ran.fetch_add(1, std::memory_order_relaxed);
              std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }));
    }

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // Try bulk operations concurrently with active threads
  registry().apply([](registered_thread_info_backend const& e)
                     { return e.component == "even"; },
                   [&](registered_thread_info_backend const& e)
                     {
                       (void)registry().set_priority(
                           e.tid, native_thread_priority{ 0 });
                     });

  for (auto& t : threads)
    {
      t->join();
    }
  EXPECT_GE(ran.load(), kThreads);
}
