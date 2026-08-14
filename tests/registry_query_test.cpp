#include <algorithm>
#include <gtest/gtest.h>
#include <set>
#include <string>
#include <threadschedule/detail/registry/registered_thread_backend.hpp>
#include <threadschedule/thread_registry.hpp>

using namespace threadschedule;
using namespace threadschedule::detail;

class RegistryQueryTest : public ::testing::Test
{
protected:
  thread_registry_backend reg_;

  void
  register_threads()
  {
    threads_.emplace_back("alpha", "io",
                          [this]
                            {
                              std::unique_lock<std::mutex> lock(mtx_);
                              cv_.wait(lock, [this] { return done_; });
                            });
    threads_.emplace_back("beta", "compute",
                          [this]
                            {
                              std::unique_lock<std::mutex> lock(mtx_);
                              cv_.wait(lock, [this] { return done_; });
                            });
    threads_.emplace_back("gamma", "io",
                          [this]
                            {
                              std::unique_lock<std::mutex> lock(mtx_);
                              cv_.wait(lock, [this] { return done_; });
                            });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  void
  SetUp() override
  {
    register_threads();
  }

  void
  TearDown() override
  {
    {
      std::lock_guard<std::mutex> lock(mtx_);
      done_ = true;
    }
    cv_.notify_all();
    threads_.clear();
  }

private:
  struct RegThread
  {
    std::thread t;
    RegThread(std::string name, std::string tag, std::function<void()> fn)
    {
      t = std::thread(
          [name = std::move(name), tag = std::move(tag), fn = std::move(fn)]
            {
              registration_guard_backend guard(name, tag);
              fn();
            });
    }
    ~RegThread()
    {
      if (t.joinable())
        t.join();
    }
    RegThread(RegThread&&) = default;
    RegThread& operator=(RegThread&&) = default;
  };
  std::vector<RegThread> threads_;
  std::mutex mtx_;
  std::condition_variable cv_;
  bool done_{ false };
};

TEST_F(RegistryQueryTest, CountReturnsAll)
{
  EXPECT_GE(runtime_registry().count(), 3u);
}

TEST_F(RegistryQueryTest, FilterByTag)
{
  auto io_count = runtime_registry().filter([](auto const& e) { return e.component == "io"; }).count();
  EXPECT_EQ(io_count, 2u);
}

TEST_F(RegistryQueryTest, FilterByName)
{
  auto result = runtime_registry().filter([](auto const& e) { return e.name == "beta"; }).count();
  EXPECT_EQ(result, 1u);
}

TEST_F(RegistryQueryTest, MapExtractsNames)
{
  auto names = runtime_registry().map([](auto const& e) { return e.name; });
  auto it_alpha = std::find(names.begin(), names.end(), "alpha");
  auto it_beta = std::find(names.begin(), names.end(), "beta");
  EXPECT_NE(it_alpha, names.end());
  EXPECT_NE(it_beta, names.end());
}

TEST_F(RegistryQueryTest, FindIfFindsMatch)
{
  auto found = runtime_registry().find_if([](auto const& e) { return e.name == "gamma"; });
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->name, "gamma");
  EXPECT_EQ(found->component, "io");
}

TEST_F(RegistryQueryTest, FindIfReturnsNulloptOnMiss)
{
  auto found = runtime_registry().find_if([](auto const& e) { return e.name == "nonexistent"; });
  EXPECT_FALSE(found.has_value());
}

TEST_F(RegistryQueryTest, AnyReturnsTrueWhenMatching)
{
  EXPECT_TRUE(runtime_registry().any([](auto const& e) { return e.component == "compute"; }));
}

TEST_F(RegistryQueryTest, AnyReturnsFalseWhenNoMatch)
{
  EXPECT_FALSE(runtime_registry().any([](auto const& e) { return e.component == "missing"; }));
}

TEST_F(RegistryQueryTest, AllReturnsFalseWhenNotAllMatch)
{
  EXPECT_FALSE(runtime_registry().all([](auto const& e) { return e.component == "io"; }));
}

TEST_F(RegistryQueryTest, NoneReturnsTrueWhenNoMatch)
{
  EXPECT_TRUE(runtime_registry().none([](auto const& e) { return e.name == "zzz"; }));
}

TEST_F(RegistryQueryTest, NoneReturnsFalseWhenMatch)
{
  EXPECT_FALSE(runtime_registry().none([](auto const& e) { return e.name == "alpha"; }));
}

TEST_F(RegistryQueryTest, TakeLimitsResults)
{
  auto view = runtime_registry().take(2);
  EXPECT_LE(view.count(), 2u);
}

TEST_F(RegistryQueryTest, SkipSkipsEntries)
{
  auto total = runtime_registry().count();
  auto skipped = runtime_registry().skip(1).count();
  if (total > 1)
    {
      EXPECT_EQ(skipped, total - 1);
    }
}

TEST_F(RegistryQueryTest, ForEachVisitsAll)
{
  size_t visited = 0;
  runtime_registry().for_each([&visited](auto const&) { ++visited; });
  EXPECT_GE(visited, 3u);
}

TEST_F(RegistryQueryTest, ChainedFilterMapForEach)
{
  auto io_names = runtime_registry()
                      .filter([](auto const& e) { return e.component == "io"; })
                      .map([](auto const& e) { return e.name; });
  EXPECT_EQ(io_names.size(), 2u);
  std::set<std::string> names(io_names.begin(), io_names.end());
  EXPECT_TRUE(names.count("alpha"));
  EXPECT_TRUE(names.count("gamma"));
}
