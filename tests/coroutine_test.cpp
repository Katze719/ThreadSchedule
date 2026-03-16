#include <threadschedule/task.hpp>
#include <threadschedule/generator.hpp>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

using namespace threadschedule;

// ── task<T> tests ───────────────────────────────────────────────────

TEST(TaskTest, BasicReturnValue)
{
    auto coro = []() -> task<int> { co_return 42; };
    int result = sync_wait(coro());
    EXPECT_EQ(result, 42);
}

TEST(TaskTest, ReturnString)
{
    auto coro = []() -> task<std::string> { co_return "hello coroutine"; };
    std::string result = sync_wait(coro());
    EXPECT_EQ(result, "hello coroutine");
}

TEST(TaskTest, VoidTask)
{
    bool executed = false;
    auto coro = [&]() -> task<void> {
        executed = true;
        co_return;
    };
    sync_wait(coro());
    EXPECT_TRUE(executed);
}

TEST(TaskTest, ExceptionPropagation)
{
    auto coro = []() -> task<int> {
        throw std::runtime_error("boom");
        co_return 0;
    };
    EXPECT_THROW(sync_wait(coro()), std::runtime_error);
}

TEST(TaskTest, VoidExceptionPropagation)
{
    auto coro = []() -> task<void> {
        throw std::logic_error("void boom");
        co_return;
    };
    EXPECT_THROW(sync_wait(coro()), std::logic_error);
}

TEST(TaskTest, NestedTaskAwait)
{
    auto inner = []() -> task<int> { co_return 21; };

    auto outer = [&]() -> task<int> {
        int val = co_await inner();
        co_return val * 2;
    };

    int result = sync_wait(outer());
    EXPECT_EQ(result, 42);
}

TEST(TaskTest, DeeplyNestedTasks)
{
    auto level3 = []() -> task<int> { co_return 10; };

    auto level2 = [&]() -> task<int> {
        int v = co_await level3();
        co_return v + 5;
    };

    auto level1 = [&]() -> task<int> {
        int v = co_await level2();
        co_return v * 2;
    };

    EXPECT_EQ(sync_wait(level1()), 30);
}

TEST(TaskTest, MoveOnlyResult)
{
    auto coro = []() -> task<std::unique_ptr<int>> { co_return std::make_unique<int>(99); };
    auto result = sync_wait(coro());
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, 99);
}

TEST(TaskTest, TaskIsMoveOnly)
{
    auto coro = []() -> task<int> { co_return 1; };
    task<int> t1 = coro();
    task<int> t2 = std::move(t1);
    EXPECT_EQ(sync_wait(std::move(t2)), 1);
}

// ── generator<T> tests ─────────────────────────────────────────────

TEST(GeneratorTest, BasicSequence)
{
    auto gen = []() -> generator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
    };

    std::vector<int> values;
    for (int v : gen())
    {
        values.push_back(v);
    }
    EXPECT_EQ(values, (std::vector<int>{1, 2, 3}));
}

TEST(GeneratorTest, EmptyGenerator)
{
    auto gen = []() -> generator<int> { co_return; };

    std::vector<int> values;
    for (int v : gen())
    {
        values.push_back(v);
    }
    EXPECT_TRUE(values.empty());
}

TEST(GeneratorTest, Fibonacci)
{
    auto fib = []() -> generator<int> {
        int a = 0;
        int b = 1;
        while (true)
        {
            co_yield a;
            int tmp = a;
            a = b;
            b = tmp + b;
        }
    };

    std::vector<int> values;
    for (int v : fib())
    {
        if (v > 20)
            break;
        values.push_back(v);
    }
    EXPECT_EQ(values, (std::vector<int>{0, 1, 1, 2, 3, 5, 8, 13}));
}

TEST(GeneratorTest, EarlyBreakDestructorSafety)
{
    int destroy_count = 0;
    struct Counter
    {
        int* count;
        ~Counter()
        {
            ++(*count);
        }
    };

    auto gen = [&]() -> generator<int> {
        Counter c{&destroy_count};
        co_yield 1;
        co_yield 2;
        co_yield 3;
    };

    {
        auto g = gen();
        for (int v : g)
        {
            if (v == 1)
                break;
        }
    }
    EXPECT_GE(destroy_count, 1);
}

TEST(GeneratorTest, StringValues)
{
    auto gen = []() -> generator<std::string> {
        co_yield std::string("alpha");
        co_yield std::string("beta");
        co_yield std::string("gamma");
    };

    std::vector<std::string> values;
    auto g = gen();
    for (const auto& v : g)
    {
        values.push_back(v);
    }
    EXPECT_EQ(values, (std::vector<std::string>{"alpha", "beta", "gamma"}));
}

TEST(GeneratorTest, ExceptionInGenerator)
{
    auto gen = []() -> generator<int> {
        co_yield 1;
        throw std::runtime_error("generator error");
        co_yield 2;
    };

    auto g = gen();
    auto it = g.begin();
    EXPECT_EQ(*it, 1);
    EXPECT_THROW(++it, std::runtime_error);
}

TEST(GeneratorTest, GeneratorIsMoveOnly)
{
    auto gen = []() -> generator<int> {
        co_yield 42;
    };

    generator<int> g1 = gen();
    generator<int> g2 = std::move(g1);

    std::vector<int> values;
    for (int v : g2)
    {
        values.push_back(v);
    }
    EXPECT_EQ(values, (std::vector<int>{42}));
}

TEST(GeneratorTest, LargeSequence)
{
    auto iota = [](int n) -> generator<int> {
        for (int i = 0; i < n; ++i)
            co_yield i;
    };

    int count = 0;
    int sum = 0;
    for (int v : iota(1000))
    {
        sum += v;
        ++count;
    }
    EXPECT_EQ(count, 1000);
    EXPECT_EQ(sum, 999 * 1000 / 2);
}

#else

// Placeholder so the test binary compiles without coroutine support
#include <gtest/gtest.h>
TEST(CoroutineTest, SkippedNoCoroutineSupport)
{
    GTEST_SKIP() << "C++20 coroutines not available";
}

#endif // __cpp_impl_coroutine
