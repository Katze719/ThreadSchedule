#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <threadschedule/detail/registered_thread_backend.hpp>
#include <threadschedule/thread_registry.hpp>
#ifndef _WIN32
#  include <sched.h>
#endif

using namespace threadschedule;

TEST(ThreadRegistryTest, RegistersAndApplies)
{
  std::atomic<bool> ran{ false };
  detail::registered_thread_backend t("treg", "test",
                                      [&]
                                        {
                                          ran = true;
                                          std::this_thread::sleep_for(
                                              std::chrono::milliseconds(100));
                                        });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // Find by tag and set a neutral priority
  bool found = false;
  registry().apply(
      [&](registered_thread_info_backend const& e)
        {
          found = found || (e.component == "test");
          return e.component == "test";
        },
      [&](registered_thread_info_backend const& e)
        {
          (void)registry().set_priority(e.tid, native_thread_priority{ 0 });
        });

  EXPECT_TRUE(found);

  t.join();
  EXPECT_TRUE(ran.load());
}

#ifndef _WIN32
TEST(ThreadRegistryTest, LinuxAffinitySet)
{
  detail::registered_thread_backend t(
      "treg2", "aff",
      [] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  native_thread_affinity aff;
  aff.clear();
  aff.add_cpu(0);

  bool attempted = false;
  registry().apply([](registered_thread_info_backend const& e)
                     { return e.component == "aff"; },
                   [&](registered_thread_info_backend const& e)
                     {
                       attempted = true;
                       (void)registry().set_affinity(e.tid, aff);
                     });

  EXPECT_TRUE(attempted);
  t.join();
}
#endif

TEST(ThreadRegistryTest, DuplicateRegistrationIsNoOp)
{
  // Register current thread manually twice and ensure the first registration
  // wins and that count remains 1 and properties are from the first call
  registry().unregister_current_thread();

  auto_register_current_thread guard1("first-name", "first-comp");

  // Attempt duplicate registration for the same thread id
  registry().register_current_thread(std::string("second-name"),
                                     std::string("second-comp"));

  // Snapshot and checks
  auto snapshot = registry().query().entries();
  ASSERT_GE(snapshot.size(), static_cast<size_t>(1));

  // Find this current thread's entry by std::thread::id
  auto selfStdId = std::this_thread::get_id();
  auto it = std::find_if(snapshot.begin(), snapshot.end(),
                         [&](registered_thread_info_backend const& e)
                           { return e.std_id == selfStdId; });
  ASSERT_TRUE(it != snapshot.end());

  // Expect first registration values to persist
  EXPECT_EQ(it->name, std::string("first-name"));
  EXPECT_EQ(it->component, std::string("first-comp"));
}

TEST(ThreadRegistryTest, CallbackOnRegisterFires)
{
  // Ensure clean state and no side effects from other tests
  registry().unregister_current_thread();

  std::atomic<int> calls{ 0 };
  std::atomic<native_thread_id> lastTid{ 0 };
  std::string lastName;
  std::string lastComp;

  registry().set_on_register(
      [&](registered_thread_info_backend const& e)
        {
          calls.fetch_add(1, std::memory_order_relaxed);
          lastTid.store(e.tid, std::memory_order_relaxed);
          lastName = e.name;
          lastComp = e.component;
        });

  {
    auto_register_current_thread guard("cb-name", "cb-comp");
    EXPECT_GE(calls.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(lastTid.load(std::memory_order_relaxed),
              thread_info::get_thread_id());
    EXPECT_EQ(lastName, std::string("cb-name"));
    EXPECT_EQ(lastComp, std::string("cb-comp"));
  }

  // Reset hook
  registry().set_on_register({});
}

TEST(ThreadRegistryTest, RegisteredThreadBackendMoveAssign)
{
  std::atomic<bool> ran{ false };
  detail::registered_thread_backend t;
  EXPECT_FALSE(t.joinable());

  t = detail::registered_thread_backend("move-tw", "move",
                                        [&]
                                          {
                                            ran = true;
                                            std::this_thread::sleep_for(
                                                std::chrono::milliseconds(50));
                                          });

  EXPECT_TRUE(t.joinable());
  t.join();
  EXPECT_TRUE(ran.load());
}

TEST(ThreadRegistryTest, CallbackOnUnregisterFires)
{
  registry().unregister_current_thread();

  std::atomic<int> calls{ 0 };
  std::atomic<native_thread_id> lastTid{ 0 };

  registry().set_on_unregister(
      [&](registered_thread_info_backend const& e)
        {
          calls.fetch_add(1, std::memory_order_relaxed);
          lastTid.store(e.tid, std::memory_order_relaxed);
        });

  native_thread_id currentTid = 0;
  {
    auto_register_current_thread guard("cb2-name", "cb2-comp");
    currentTid = thread_info::get_thread_id();
  } // guard dtor should unregister

  EXPECT_GE(calls.load(std::memory_order_relaxed), 1);
  EXPECT_EQ(lastTid.load(std::memory_order_relaxed), currentTid);

  // Reset hook
  registry().set_on_unregister({});
}
