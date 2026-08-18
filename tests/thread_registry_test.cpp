#include <algorithm>
#include <atomic>
#include <future>
#include <gtest/gtest.h>
#include <thread>
#include <threadschedule/detail/registry/registered_thread_backend.hpp>
#include <threadschedule/thread_registry.hpp>
#ifndef _WIN32
#  include <sched.h>
#endif

using namespace threadschedule;
using namespace threadschedule::detail;

TEST(ThreadRegistryTest, RegistersAndApplies)
{
  std::atomic<bool> ran{ false };
  detail::registered_thread_backend t("treg", "test",
                                      [&]
                                        {
                                          ran = true;
                                          std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                        });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // Find by tag and set a neutral priority
  bool found = false;
  runtime_registry().apply(
      [&](registered_thread_info_backend const& e)
        {
          found = found || (e.component == "test");
          return e.component == "test";
        },
      [&](registered_thread_info_backend const& e)
        { (void)runtime_registry().set_priority(e.tid, native_thread_priority{ 0 }); });

  EXPECT_TRUE(found);

  t.join();
  EXPECT_TRUE(ran.load());
}

#ifndef _WIN32
TEST(ThreadRegistryTest, LinuxAffinitySet)
{
  detail::registered_thread_backend t("treg2", "aff",
                                      [] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  native_thread_affinity aff;
  aff.clear();
  aff.add_cpu(0);

  bool attempted = false;
  runtime_registry().apply([](registered_thread_info_backend const& e) { return e.component == "aff"; },
                           [&](registered_thread_info_backend const& e)
                             {
                               attempted = true;
                               (void)runtime_registry().set_affinity(e.tid, aff);
                             });

  EXPECT_TRUE(attempted);
  t.join();
}
#endif

TEST(ThreadRegistryTest, DuplicateRegistrationIsNoOp)
{
  // Register current thread manually twice and ensure the first registration
  // wins and that count remains 1 and properties are from the first call
  runtime_registry().unregister_current_thread();

  registration_guard_backend guard1("first-name", "first-comp");

  // Attempt duplicate registration for the same thread id
  runtime_registry().register_current_thread(std::string("second-name"), std::string("second-comp"));

  // Snapshot and checks
  auto snapshot = runtime_registry().query().entries();
  ASSERT_GE(snapshot.size(), static_cast<size_t>(1));

  // Find this current thread's entry by std::thread::id
  auto selfStdId = std::this_thread::get_id();
  auto it = std::find_if(snapshot.begin(), snapshot.end(),
                         [&](registered_thread_info_backend const& e) { return e.std_id == selfStdId; });
  ASSERT_TRUE(it != snapshot.end());

  // Expect first registration values to persist
  EXPECT_EQ(it->name, std::string("first-name"));
  EXPECT_EQ(it->component, std::string("first-comp"));
}

TEST(ThreadRegistryTest, NestedRegistrationGuardDoesNotRemoveOuterEntry)
{
  thread_registry_backend local;
  {
    registration_guard_backend outer(local, "outer", "test");
    EXPECT_EQ(local.count(), 1u);
    {
      registration_guard_backend inner(local, "inner", "test");
      EXPECT_EQ(local.count(), 1u);
    }
    EXPECT_EQ(local.count(), 1u);
    auto entry = local.get(thread_info::get_thread_id());
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->name, "outer");
  }
  EXPECT_TRUE(local.empty());
}

TEST(ThreadRegistryTest, GlobalGuardUnregistersFromCapturedRegistry)
{
  thread_registry_backend first;
  thread_registry_backend second;
  runtime_set_external_registry(&first);
  {
    registration_guard_backend guard("captured", "test");
    EXPECT_EQ(first.count(), 1u);
    runtime_set_external_registry(&second);
  }
  runtime_set_external_registry(nullptr);

  EXPECT_TRUE(first.empty());
  EXPECT_TRUE(second.empty());
}

TEST(ThreadRegistryTest, StaleControlBlockRejectsOperations)
{
  thread_registry_backend local;
  auto control = thread_control_block::create_for_current_thread();
  local.register_current_thread(control, "stale", "test");
  local.unregister_current_thread();

  auto result = control->set_priority(native_thread_priority::normal());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::no_such_process));
}

#ifndef _WIN32
TEST(ThreadRegistryTest, StandaloneControlCannotAffectCallerAfterThreadExit)
{
  auto main_affinity = thread_info().get_affinity();
  ASSERT_TRUE(main_affinity.has_value());
  ASSERT_FALSE(main_affinity->get_cpus().empty());

  std::promise<std::shared_ptr<thread_control_block>> published;
  std::thread worker([&published] { published.set_value(thread_control_block::create_for_current_thread()); });
  auto control = published.get_future().get();
  worker.join();

  native_thread_affinity requested({ main_affinity->get_cpus().front() });
  auto result = control->set_affinity(requested);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::no_such_process));

  auto after = thread_info().get_affinity();
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(after->get_cpus(), main_affinity->get_cpus());
}

TEST(ThreadRegistryTest, AffinityRejectsAndRollsBackPartialApplication)
{
  thread_registry_backend local;
  std::promise<std::shared_ptr<thread_control_block>> published;
  std::promise<void> release;
  auto release_ready = release.get_future().share();
  std::thread worker(
      [&]
        {
          auto control = thread_control_block::create_for_current_thread();
          local.register_current_thread(control, "affinity", "test");
          published.set_value(control);
          release_ready.wait();
          local.unregister_current_thread();
        });
  auto control = published.get_future().get();
  auto original = thread_info(control->tid()).get_affinity();
  if (!original || original->get_cpus().empty())
    {
      release.set_value();
      worker.join();
      GTEST_SKIP() << "Cannot read worker affinity";
    }

  int unavailable = -1;
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
    if (!original->has_cpu(cpu))
      {
        unavailable = cpu;
        break;
      }
  if (unavailable < 0)
    {
      release.set_value();
      worker.join();
      GTEST_SKIP() << "No unavailable CPU within cpu_set_t";
    }

  native_thread_affinity requested({ original->get_cpus().front(), unavailable });
  auto result = control->set_affinity(requested);
  auto effective = thread_info(control->tid()).get_affinity();
  release.set_value();
  worker.join();

  ASSERT_FALSE(result.has_value());
  ASSERT_TRUE(effective.has_value());
  EXPECT_EQ(effective->get_cpus(), original->get_cpus());
}
#endif

TEST(ThreadRegistryTest, GuardDoesNotRemoveReplacementRegistration)
{
  thread_registry_backend local;
  std::shared_ptr<thread_control_block> replacement;
  bool replace = true;
  local.set_on_register(
      [&](registered_thread_info_backend const&)
        {
          if (!replace)
            return;
          replace = false;
          local.unregister_current_thread();
          replacement = thread_control_block::create_for_current_thread();
          local.register_current_thread(replacement, "replacement", "test");
        });

  {
    registration_guard_backend guard(local, "original", "test");
    auto entry = local.get(thread_info::get_thread_id());
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->control, replacement);
  }

  EXPECT_EQ(local.count(), 1u);
  local.unregister_current_thread();
}

TEST(ThreadRegistryTest, ThrowingCallbacksDoNotEscapeRegistryOperations)
{
  thread_registry_backend local;
  local.set_on_register([](registered_thread_info_backend const&) { throw std::runtime_error("register callback"); });
  local.set_on_unregister([](registered_thread_info_backend const&)
                            { throw std::runtime_error("unregister callback"); });

  EXPECT_NO_THROW({
    registration_guard_backend guard(local, "callbacks", "test");
    EXPECT_EQ(local.count(), 1u);
  });
  EXPECT_TRUE(local.empty());
}

TEST(ThreadRegistryTest, CallbackOnRegisterFires)
{
  // Ensure clean state and no side effects from other tests
  runtime_registry().unregister_current_thread();

  std::atomic<int> calls{ 0 };
  std::atomic<native_thread_id> lastTid{ 0 };
  std::string lastName;
  std::string lastComp;

  runtime_registry().set_on_register(
      [&](registered_thread_info_backend const& e)
        {
          calls.fetch_add(1, std::memory_order_relaxed);
          lastTid.store(e.tid, std::memory_order_relaxed);
          lastName = e.name;
          lastComp = e.component;
        });

  {
    registration_guard_backend guard("cb-name", "cb-comp");
    EXPECT_GE(calls.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(lastTid.load(std::memory_order_relaxed), thread_info::get_thread_id());
    EXPECT_EQ(lastName, std::string("cb-name"));
    EXPECT_EQ(lastComp, std::string("cb-comp"));
  }

  // Reset hook
  runtime_registry().set_on_register({});
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
                                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                                          });

  EXPECT_TRUE(t.joinable());
  t.join();
  EXPECT_TRUE(ran.load());
}

TEST(ThreadRegistryTest, RegisteredThreadBackendAcceptsPackagedTask)
{
  std::packaged_task<int()> task([]() { return 42; });
  auto result = task.get_future();

  detail::registered_thread_backend t("packaged", "move-only", std::move(task));

  t.join();
  EXPECT_EQ(result.get(), 42);
}

TEST(ThreadRegistryTest, CallbackOnUnregisterFires)
{
  runtime_registry().unregister_current_thread();

  std::atomic<int> calls{ 0 };
  std::atomic<native_thread_id> lastTid{ 0 };

  runtime_registry().set_on_unregister(
      [&](registered_thread_info_backend const& e)
        {
          calls.fetch_add(1, std::memory_order_relaxed);
          lastTid.store(e.tid, std::memory_order_relaxed);
        });

  native_thread_id currentTid = 0;
  {
    registration_guard_backend guard("cb2-name", "cb2-comp");
    currentTid = thread_info::get_thread_id();
  } // guard dtor should unregister

  EXPECT_GE(calls.load(std::memory_order_relaxed), 1);
  EXPECT_EQ(lastTid.load(std::memory_order_relaxed), currentTid);

  // Reset hook
  runtime_registry().set_on_unregister({});
}
