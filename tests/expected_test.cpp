#include <gtest/gtest.h>
#include <system_error>
#include <threadschedule/expected.hpp>

namespace ts = threadschedule;
using namespace threadschedule;

namespace
{

enum class parse_error
{
    invalid_input = 1,
    overflow = 2
};

ts::expected<int, std::error_code> parse_int_ok()
{
    return 42;
}

ts::expected<int, std::error_code> parse_int_fail()
{
    return ts::unexpected(std::make_error_code(std::errc::invalid_argument));
}

ts::expected<void, std::error_code> do_void_ok()
{
    return {};
}

ts::expected<void, std::error_code> do_void_fail()
{
    return ts::unexpected(std::make_error_code(std::errc::operation_not_permitted));
}

} // namespace

TEST(ExpectedTest, ValueConstruction)
{
    auto r = parse_int_ok();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 42);
}

TEST(ExpectedTest, UnexpectedConstruction)
{
    auto r = parse_int_fail();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), std::make_error_code(std::errc::invalid_argument));
}

TEST(ExpectedTest, ValueOr)
{
    auto ok = parse_int_ok();
    auto bad = parse_int_fail();
    EXPECT_EQ(ok.value_or(7), 42);
    EXPECT_EQ(bad.value_or(7), 7);
}

TEST(ExpectedTest, VoidOk)
{
    auto r = do_void_ok();
    EXPECT_TRUE(r.has_value());
}

TEST(ExpectedTest, VoidFail)
{
    auto r = do_void_fail();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), std::make_error_code(std::errc::operation_not_permitted));
}

TEST(ExpectedTest, IfConditionWorks)
{
    auto r1 = parse_int_ok();
    if (r1)
    {
        EXPECT_EQ(r1.value(), 42);
    }
    else
    {
        FAIL() << "Expected success";
    }

    auto r2 = parse_int_fail();
    if (r2)
    {
        FAIL() << "Expected failure";
    }
    else
    {
        EXPECT_EQ(r2.error(), std::make_error_code(std::errc::invalid_argument));
    }
}

TEST(ExpectedTest, IfConditionWorksVoid)
{
    auto ok = do_void_ok();
    if (ok)
    {
        SUCCEED();
    }
    else
    {
        FAIL() << "Expected success";
    }

    auto bad = do_void_fail();
    if (bad)
    {
        FAIL() << "Expected failure";
    }
    else
    {
        EXPECT_EQ(bad.error(), std::make_error_code(std::errc::operation_not_permitted));
    }
}

TEST(ExpectedTest, TypeAliases)
{
    using exp_t = ts::expected<int, std::error_code>;
    static_assert(std::is_same_v<exp_t::value_type, int>);
    static_assert(std::is_same_v<exp_t::error_type, std::error_code>);
    static_assert(std::is_same_v<exp_t::unexpected_type, ts::unexpected<std::error_code>>);

    using exp_void_t = ts::expected<void, std::error_code>;
    static_assert(std::is_same_v<exp_void_t::value_type, void>);
    static_assert(std::is_same_v<exp_void_t::error_type, std::error_code>);
}

TEST(ExpectedTest, OperatorStar)
{
    auto r = parse_int_ok();
    EXPECT_EQ(*r, 42);

    auto const cr = parse_int_ok();
    EXPECT_EQ(*cr, 42);
}

TEST(ExpectedTest, OperatorArrow)
{
    struct Point
    {
        int x;
        int y;
    };

    ts::expected<Point, std::error_code> p(Point{3, 4});
    EXPECT_EQ(p->x, 3);
    EXPECT_EQ(p->y, 4);

    ts::expected<Point, std::error_code> const cp(Point{5, 6});
    EXPECT_EQ(cp->x, 5);
    EXPECT_EQ(cp->y, 6);
}

TEST(ExpectedTest, ValueThrows)
{
    auto bad = parse_int_fail();
    EXPECT_THROW(bad.value(), bad_expected_access<std::error_code>);

    try
    {
        bad.value();
        FAIL() << "Should have thrown";
    }
    catch (bad_expected_access<std::error_code> const& e)
    {
        EXPECT_EQ(e.error(), std::make_error_code(std::errc::invalid_argument));
    }
}

TEST(ExpectedTest, VoidValueThrows)
{
    auto bad = do_void_fail();
    EXPECT_THROW(bad.value(), bad_expected_access<std::error_code>);
}

TEST(ExpectedTest, Emplace)
{
    ts::expected<int, std::error_code> r = parse_int_fail();
    EXPECT_FALSE(r.has_value());

    r.emplace(100);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, 100);
}

TEST(ExpectedTest, VoidEmplace)
{
    ts::expected<void, std::error_code> r = do_void_fail();
    EXPECT_FALSE(r.has_value());

    r.emplace();
    EXPECT_TRUE(r.has_value());
}

TEST(ExpectedTest, Swap)
{
    auto r1 = parse_int_ok();
    auto r2 = parse_int_fail();

    r1.swap(r2);

    EXPECT_FALSE(r1.has_value());
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(*r2, 42);
}

TEST(ExpectedTest, VoidSwap)
{
    auto r1 = do_void_ok();
    auto r2 = do_void_fail();

    r1.swap(r2);

    EXPECT_FALSE(r1.has_value());
    EXPECT_TRUE(r2.has_value());
}

TEST(ExpectedTest, AndThen)
{
    auto ok = parse_int_ok();
    auto result = ok.and_then([](int v) { return ts::expected<int, std::error_code>(v * 2); });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 84);

    auto bad = parse_int_fail();
    auto result2 = bad.and_then([](int v) { return ts::expected<int, std::error_code>(v * 2); });
    EXPECT_FALSE(result2.has_value());
}

TEST(ExpectedTest, OrElse)
{
    auto ok = parse_int_ok();
    auto result = ok.or_else([](std::error_code) { return ts::expected<int, std::error_code>(0); });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);

    auto bad = parse_int_fail();
    auto result2 = bad.or_else([](std::error_code) { return ts::expected<int, std::error_code>(99); });
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(*result2, 99);
}

TEST(ExpectedTest, Transform)
{
    auto ok = parse_int_ok();
    auto result = ok.transform([](int v) { return v * 2; });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 84);

    auto bad = parse_int_fail();
    auto result2 = bad.transform([](int v) { return v * 2; });
    EXPECT_FALSE(result2.has_value());
}

TEST(ExpectedTest, TransformError)
{
    auto bad = parse_int_fail();
    auto result = bad.transform_error([](std::error_code ec) { return ec.value(); });
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), static_cast<int>(std::errc::invalid_argument));
}

TEST(ExpectedTest, VoidAndThen)
{
    auto ok = do_void_ok();
    auto result = ok.and_then([]() { return ts::expected<void, std::error_code>(); });
    EXPECT_TRUE(result.has_value());

    auto bad = do_void_fail();
    auto result2 = bad.and_then([]() { return ts::expected<void, std::error_code>(); });
    EXPECT_FALSE(result2.has_value());
}

TEST(ExpectedTest, VoidTransform)
{
    auto ok = do_void_ok();
    auto result = ok.transform([]() { return 42; });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);

    auto bad = do_void_fail();
    auto result2 = bad.transform([]() { return 42; });
    EXPECT_FALSE(result2.has_value());
}

TEST(ExpectedTest, EqualityOperators)
{
    auto ok1 = parse_int_ok();
    auto ok2 = ts::expected<int, std::error_code>(42);
    auto ok3 = ts::expected<int, std::error_code>(43);
    auto bad = parse_int_fail();

    EXPECT_TRUE(ok1 == ok2);
    EXPECT_FALSE(ok1 != ok2);
    EXPECT_FALSE(ok1 == ok3);
    EXPECT_TRUE(ok1 != ok3);
    EXPECT_FALSE(ok1 == bad);
    EXPECT_TRUE(ok1 != bad);
}

TEST(ExpectedTest, EqualityWithValue)
{
    auto ok = parse_int_ok();
    EXPECT_TRUE(ok == 42);
    EXPECT_TRUE(42 == ok);
    EXPECT_FALSE(ok != 42);
    EXPECT_FALSE(42 != ok);
    EXPECT_FALSE(ok == 43);
    EXPECT_TRUE(ok != 43);
}

TEST(ExpectedTest, EqualityWithUnexpected)
{
    auto bad = parse_int_fail();
    auto unexp = ts::unexpected(std::make_error_code(std::errc::invalid_argument));

    EXPECT_TRUE(bad == unexp);
    EXPECT_TRUE(unexp == bad);
    EXPECT_FALSE(bad != unexp);
    EXPECT_FALSE(unexp != bad);

    auto ok = parse_int_ok();
    EXPECT_FALSE(ok == unexp);
    EXPECT_TRUE(ok != unexp);
}

TEST(ExpectedTest, InPlaceConstruction)
{
    struct Complex
    {
        int a;
        double b;
        Complex(int x, double y) : a(x), b(y)
        {
        }
    };

    ts::expected<Complex, std::error_code> e(std::in_place, 42, 3.14);
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->a, 42);
    EXPECT_DOUBLE_EQ(e->b, 3.14);
}

TEST(ExpectedTest, UnexpectConstruction)
{
    ts::expected<int, std::error_code> e(ts::unexpect, std::make_error_code(std::errc::invalid_argument));
    EXPECT_FALSE(e.has_value());
    EXPECT_EQ(e.error(), std::make_error_code(std::errc::invalid_argument));
}

TEST(ExpectedTest, ValueOrRvalue)
{
    ts::expected<int, std::error_code> ok(42);
    EXPECT_EQ(std::move(ok).value_or(7), 42);

    ts::expected<int, std::error_code> bad(ts::unexpect, std::make_error_code(std::errc::invalid_argument));
    EXPECT_EQ(std::move(bad).value_or(7), 7);
}
