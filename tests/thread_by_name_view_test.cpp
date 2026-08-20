#include <threadschedule/advanced/thread_by_name_view.hpp>
#include <threadschedule/this_thread.hpp>
#include <threadschedule/thread_view.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <string>
#include <system_error>
#include <thread>

namespace advanced = threadschedule::advanced;

#ifndef _WIN32
namespace
{

class named_waiting_thread
{
public:
  explicit named_waiting_thread(std::string const& name)
  {
    auto released = release_.get_future();
    thread_ = std::thread([released = std::move(released)]() mutable { released.wait(); });
    threadschedule::thread_view view(thread_);
    auto named = view.set_name(name);
    if (!named)
      {
        release_.set_value();
        thread_.join();
        throw std::system_error(named.error(), "named_waiting_thread");
      }
  }

  ~named_waiting_thread()
  {
    finish();
  }

  named_waiting_thread(named_waiting_thread const&) = delete;
  auto operator=(named_waiting_thread const&) -> named_waiting_thread& = delete;

  void
  finish()
  {
    if (!thread_.joinable())
      return;
    release_.set_value();
    thread_.join();
  }

private:
  std::promise<void> release_;
  std::thread thread_;
};

template <typename T>
void
expect_no_such_process(threadschedule::result<T> const& result)
{
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::no_such_process));
}

auto
wait_until_dead(advanced::thread_by_name_view const& view) -> bool
{
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (view.alive() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return !view.alive();
}

} // namespace

TEST(ThreadByNameView, ConstructorCreateAndFindAllResolveUniqueThread)
{
  named_waiting_thread target("ts_v3_unique");

  auto created = advanced::thread_by_name_view::create("ts_v3_unique");
  ASSERT_TRUE(created.has_value());
  EXPECT_TRUE(created->alive());
  auto created_name = created->get_name();
  ASSERT_TRUE(created_name.has_value());
  EXPECT_EQ(created_name.value(), "ts_v3_unique");

  advanced::thread_by_name_view constructed("ts_v3_unique");
  EXPECT_EQ(constructed.native_id(), created->native_id());

  auto all = advanced::thread_by_name_view::find_all("ts_v3_unique");
  ASSERT_TRUE(all.has_value());
  ASSERT_EQ(all->size(), 1u);
  EXPECT_EQ(all->front().native_id(), created->native_id());
}

TEST(ThreadByNameView, ReportsInvalidMissingAndAmbiguousLookups)
{
  expect_no_such_process(advanced::thread_by_name_view::create("ts_v3_missing"));
  try
    {
      advanced::thread_by_name_view missing("ts_v3_missing");
      (void)missing;
      FAIL() << "missing lookup did not throw";
    }
  catch (std::system_error const& error)
    {
      EXPECT_EQ(error.code(), std::make_error_code(std::errc::no_such_process));
    }
  auto none = advanced::thread_by_name_view::find_all("ts_v3_missing");
  ASSERT_TRUE(none.has_value());
  EXPECT_TRUE(none->empty());

  auto empty = advanced::thread_by_name_view::create("");
  ASSERT_FALSE(empty.has_value());
  EXPECT_EQ(empty.error(), std::make_error_code(std::errc::invalid_argument));
  auto long_name = advanced::thread_by_name_view::create("sixteen-byte-nam");
  ASSERT_FALSE(long_name.has_value());
  EXPECT_EQ(long_name.error(), std::make_error_code(std::errc::invalid_argument));

  named_waiting_thread first("ts_v3_duplicate");
  named_waiting_thread second("ts_v3_duplicate");
  auto all = advanced::thread_by_name_view::find_all("ts_v3_duplicate");
  ASSERT_TRUE(all.has_value());
  ASSERT_EQ(all->size(), 2u);
  EXPECT_LT((*all)[0].native_id(), (*all)[1].native_id());

  auto unique = advanced::thread_by_name_view::create("ts_v3_duplicate");
  ASSERT_FALSE(unique.has_value());
  EXPECT_EQ(unique.error(), std::make_error_code(std::errc::invalid_argument));
  try
    {
      advanced::thread_by_name_view ambiguous("ts_v3_duplicate");
      (void)ambiguous;
      FAIL() << "ambiguous lookup did not throw";
    }
  catch (std::system_error const& error)
    {
      EXPECT_EQ(error.code(), std::make_error_code(std::errc::invalid_argument));
    }
}

TEST(ThreadByNameView, RenameKeepsOriginalIdentity)
{
  named_waiting_thread target("ts_v3_before");
  auto view = advanced::thread_by_name_view::create("ts_v3_before");
  ASSERT_TRUE(view.has_value());
  auto const id = view->native_id();

  ASSERT_TRUE(view->set_name("ts_v3_after").has_value());
  EXPECT_TRUE(view->alive());
  auto current_name = view->get_name();
  ASSERT_TRUE(current_name.has_value());
  EXPECT_EQ(current_name.value(), "ts_v3_after");
  expect_no_such_process(advanced::thread_by_name_view::create("ts_v3_before"));

  auto renamed = advanced::thread_by_name_view::create("ts_v3_after");
  ASSERT_TRUE(renamed.has_value());
  EXPECT_EQ(renamed->native_id(), id);
}

TEST(ThreadByNameView, ProvidesPortableControlParity)
{
  auto allowed = threadschedule::this_thread::get_affinity();
  ASSERT_TRUE(allowed.has_value());
  ASSERT_FALSE(allowed->empty());
  threadschedule::thread_affinity const one_cpu({ allowed->cpus().front() });

  named_waiting_thread target("ts_v3_control");
  auto view = advanced::thread_by_name_view::create("ts_v3_control");
  ASSERT_TRUE(view.has_value());

  ASSERT_TRUE(view->set_affinity(one_cpu).has_value());
  auto affinity = view->get_affinity();
  ASSERT_TRUE(affinity.has_value());
  EXPECT_EQ(affinity->cpus(), one_cpu.cpus());

  ASSERT_TRUE(view->set_priority(threadschedule::priority_level::lowest).has_value());
  auto priority = view->get_priority();
  ASSERT_TRUE(priority.has_value());
  EXPECT_EQ(priority.value(), threadschedule::priority_level::lowest);
  ASSERT_TRUE(view->set_nice(threadschedule::nice_value{ 19 }).has_value());
  auto nice = view->get_nice();
  ASSERT_TRUE(nice.has_value());
  EXPECT_EQ(nice->value(), 19);

  threadschedule::thread_config config;
  config.set_name("ts_v3_config")
      .set_scheduling(threadschedule::schedule::nice(threadschedule::nice_value{ 19 }))
      .set_affinity(one_cpu);
  ASSERT_TRUE(view->configure(config).has_value());
  auto configured_name = view->get_name();
  ASSERT_TRUE(configured_name.has_value());
  EXPECT_EQ(configured_name.value(), "ts_v3_config");
}

TEST(ThreadByNameView, RejectsEveryControlAfterTargetExit)
{
  auto allowed = threadschedule::this_thread::get_affinity();
  ASSERT_TRUE(allowed.has_value());
  ASSERT_FALSE(allowed->empty());
  threadschedule::thread_affinity const one_cpu({ allowed->cpus().front() });

  named_waiting_thread target("ts_v3_exiting");
  auto view = advanced::thread_by_name_view::create("ts_v3_exiting");
  ASSERT_TRUE(view.has_value());
  target.finish();

  ASSERT_TRUE(wait_until_dead(*view));
  expect_no_such_process(view->configure(threadschedule::thread_config{}));
  expect_no_such_process(view->set_name("ts_v3_stale"));
  expect_no_such_process(view->get_name());
  expect_no_such_process(view->set_priority(threadschedule::priority_level::lowest));
  expect_no_such_process(view->get_priority());
  expect_no_such_process(view->set_nice(threadschedule::nice_value{ 19 }));
  expect_no_such_process(view->get_nice());
  expect_no_such_process(view->set_affinity(one_cpu));
  expect_no_such_process(view->get_affinity());
}

#else

TEST(ThreadByNameView, WindowsReportsUnsupportedLookup)
{
  auto created = advanced::thread_by_name_view::create("worker");
  ASSERT_FALSE(created.has_value());
  EXPECT_EQ(created.error(), std::make_error_code(std::errc::function_not_supported));

  auto all = advanced::thread_by_name_view::find_all("worker");
  ASSERT_FALSE(all.has_value());
  EXPECT_EQ(all.error(), std::make_error_code(std::errc::function_not_supported));

  try
    {
      advanced::thread_by_name_view unsupported("worker");
      (void)unsupported;
      FAIL() << "unsupported lookup did not throw";
    }
  catch (std::system_error const& error)
    {
      EXPECT_EQ(error.code(), std::make_error_code(std::errc::function_not_supported));
    }
}

#endif
