#include <gtest/gtest.h>
#include <threadschedule/threadschedule.hpp>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

using namespace threadschedule;

TEST(CoroutinePool, ScheduleOnMovesToPoolThread)
{
    HighPerformancePool pool(2);
    auto main_tid = std::this_thread::get_id();

    auto coro = [&pool, main_tid]() -> task<std::thread::id> {
        co_await schedule_on{pool};
        co_return std::this_thread::get_id();
    };

    auto worker_tid = sync_wait(coro());
    EXPECT_NE(worker_tid, main_tid);
}

TEST(CoroutinePool, RunOnReturnsValue)
{
    HighPerformancePool pool(2);

    auto future = run_on(pool, []() -> task<int> { co_return 42; });
    EXPECT_EQ(future.get(), 42);
}

TEST(CoroutinePool, RunOnVoid)
{
    HighPerformancePool pool(2);
    std::atomic<bool> ran{false};

    auto future = run_on(pool, [&ran]() -> task<void> {
        ran = true;
        co_return;
    });
    future.get();
    EXPECT_TRUE(ran);
}

TEST(CoroutinePool, RunOnExecutesOnPoolThread)
{
    ThreadPool pool(2);
    auto main_tid = std::this_thread::get_id();

    auto future = run_on(pool, [main_tid]() -> task<bool> {
        co_return std::this_thread::get_id() != main_tid;
    });
    EXPECT_TRUE(future.get());
}

TEST(CoroutinePool, ScheduleOnWithNestedAwait)
{
    HighPerformancePool pool(2);

    auto inner = []() -> task<int> { co_return 7; };

    auto outer = [&pool, &inner]() -> task<int> {
        co_await schedule_on{pool};
        int v = co_await inner();
        co_return v * 6;
    };

    EXPECT_EQ(sync_wait(outer()), 42);
}

TEST(CoroutinePool, PoolExecutorCanBeSetOnTask)
{
    ThreadPool pool(2);
    pool_executor<ThreadPool> exec(pool);

    auto coro = []() -> task<int> { co_return 10; };
    EXPECT_EQ(sync_wait(coro()), 10);
}

TEST(CoroutinePool, RunOnPropagatesException)
{
    ThreadPool pool(2);

    auto future = run_on(pool, []() -> task<int> {
        throw std::runtime_error("oops");
        co_return 0;
    });
    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST(CoroutinePool, MultipleScheduleOnHops)
{
    HighPerformancePool pool1(2);
    ThreadPool pool2(2);

    auto coro = [&pool1, &pool2]() -> task<int> {
        co_await schedule_on{pool1};
        auto tid1 = std::this_thread::get_id();
        co_await schedule_on{pool2};
        auto tid2 = std::this_thread::get_id();
        (void)tid1;
        (void)tid2;
        co_return 99;
    };

    EXPECT_EQ(sync_wait(coro()), 99);
}

#else

TEST(CoroutinePool, SkippedNoCoroutineSupport)
{
    GTEST_SKIP() << "Coroutine support not available";
}

#endif
