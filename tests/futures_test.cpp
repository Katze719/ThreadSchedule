#include <gtest/gtest.h>
#include <threadschedule/futures.hpp>
#include <threadschedule/thread_pool.hpp>
#include <future>
#include <stdexcept>
#include <string>
#include <vector>

using namespace threadschedule;

// ==================== when_all (non-void) ====================

TEST(FuturesTest, WhenAllCollectsResults)
{
    ThreadPool pool(2);
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 5; ++i)
        futures.push_back(pool.submit([i] { return i * 10; }));

    auto results = when_all(futures);
    ASSERT_EQ(results.size(), 5u);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(results[i], i * 10);
}

TEST(FuturesTest, WhenAllRethrowsFirstException)
{
    ThreadPool pool(2);
    std::vector<std::future<int>> futures;
    futures.push_back(pool.submit([] { return 1; }));
    futures.push_back(pool.submit([]() -> int { throw std::runtime_error("boom"); }));
    futures.push_back(pool.submit([] { return 3; }));

    EXPECT_THROW(when_all(futures), std::runtime_error);
}

TEST(FuturesTest, WhenAllEmptyVector)
{
    std::vector<std::future<int>> empty;
    auto results = when_all(empty);
    EXPECT_TRUE(results.empty());
}

// ==================== when_all (void) ====================

TEST(FuturesTest, WhenAllVoidCompletes)
{
    ThreadPool pool(2);
    std::atomic<int> count{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 5; ++i)
        futures.push_back(pool.submit([&count] { count.fetch_add(1, std::memory_order_relaxed); }));

    when_all(futures);
    EXPECT_EQ(count.load(), 5);
}

TEST(FuturesTest, WhenAllVoidRethrowsException)
{
    ThreadPool pool(2);
    std::vector<std::future<void>> futures;
    futures.push_back(pool.submit([] {}));
    futures.push_back(pool.submit([] { throw std::logic_error("fail"); }));

    EXPECT_THROW(when_all(futures), std::logic_error);
}

// ==================== when_all_settled (non-void) ====================

TEST(FuturesTest, WhenAllSettledSuccess)
{
    ThreadPool pool(2);
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 3; ++i)
        futures.push_back(pool.submit([i] { return i; }));

    auto results = when_all_settled(futures);
    ASSERT_EQ(results.size(), 3u);
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(results[i].has_value());
        EXPECT_EQ(results[i].value(), i);
    }
}

TEST(FuturesTest, WhenAllSettledWithExceptions)
{
    ThreadPool pool(2);
    std::vector<std::future<int>> futures;
    futures.push_back(pool.submit([] { return 1; }));
    futures.push_back(pool.submit([]() -> int { throw std::runtime_error("err"); }));
    futures.push_back(pool.submit([] { return 3; }));

    auto results = when_all_settled(futures);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_TRUE(results[0].has_value());
    EXPECT_FALSE(results[1].has_value());
    EXPECT_TRUE(results[2].has_value());
}

// ==================== when_all_settled (void) ====================

TEST(FuturesTest, WhenAllSettledVoid)
{
    ThreadPool pool(2);
    std::vector<std::future<void>> futures;
    futures.push_back(pool.submit([] {}));
    futures.push_back(pool.submit([] { throw std::runtime_error("fail"); }));
    futures.push_back(pool.submit([] {}));

    auto results = when_all_settled(futures);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_TRUE(results[0].has_value());
    EXPECT_FALSE(results[1].has_value());
    EXPECT_TRUE(results[2].has_value());
}

// ==================== when_any (non-void) ====================

TEST(FuturesTest, WhenAnyReturnsFirst)
{
    ThreadPool pool(4);
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 3; ++i)
        futures.push_back(pool.submit([i] { return i + 100; }));

    auto [idx, value] = when_any(futures);
    EXPECT_LT(idx, 3u);
    EXPECT_EQ(value, static_cast<int>(idx) + 100);
}

TEST(FuturesTest, WhenAnyEmptyThrows)
{
    std::vector<std::future<int>> empty;
    EXPECT_THROW(when_any(empty), std::invalid_argument);
}

TEST(FuturesTest, WhenAnySingleFuture)
{
    ThreadPool pool(1);
    std::vector<std::future<int>> futures;
    futures.push_back(pool.submit([] { return 42; }));

    auto [idx, value] = when_any(futures);
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(value, 42);
}

// ==================== when_any (void) ====================

TEST(FuturesTest, WhenAnyVoidReturnsFirst)
{
    ThreadPool pool(4);
    std::atomic<int> count{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 3; ++i)
        futures.push_back(pool.submit([&count] { count.fetch_add(1, std::memory_order_relaxed); }));

    size_t idx = when_any(futures);
    EXPECT_LT(idx, 3u);
}

TEST(FuturesTest, WhenAnyVoidEmptyThrows)
{
    std::vector<std::future<void>> empty;
    EXPECT_THROW(when_any(empty), std::invalid_argument);
}

TEST(FuturesTest, WhenAnyVoidPropagatesException)
{
    ThreadPool pool(1);
    std::vector<std::future<void>> futures;
    futures.push_back(pool.submit([] { throw std::runtime_error("boom"); }));

    EXPECT_THROW(when_any(futures), std::runtime_error);
}
