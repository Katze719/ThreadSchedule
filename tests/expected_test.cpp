#include <array>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <system_error>
#include <threadschedule/expected.hpp>
#include <vector>

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

// Additional tests with std::string as value and/or error type
TEST(ExpectedStringTest, StringValueBasic)
{
    ts::expected<std::string, std::error_code> e(std::string("hello"));
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e.value(), std::string("hello"));
    EXPECT_EQ((*e).size(), 5u);
    EXPECT_EQ(e->size(), 5u);
}

TEST(ExpectedStdTypes, VectorValueBasicAndThen)
{
    ts::expected<std::vector<int>, std::string> ok(std::vector<int>{1, 2, 3});
    ASSERT_TRUE(ok.has_value());
    auto sizes = ok.and_then([](std::vector<int>& v) { return ts::expected<std::size_t, std::string>(v.size()); });
    ASSERT_TRUE(sizes.has_value());
    EXPECT_EQ(*sizes, 3U);
}

TEST(ExpectedStdTypes, VectorOrElseFallback)
{
    ts::expected<std::vector<int>, std::string> bad(ts::unexpect, std::string("err"));
    auto res =
        bad.or_else([](std::string) { return ts::expected<std::vector<int>, std::string>(std::vector<int>{7}); });
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->size(), 1U);
    EXPECT_EQ((*res)[0], 7);
}

TEST(ExpectedStdTypes, OptionalValueTransform)
{
    ts::expected<std::optional<int>, std::error_code> ok(std::optional<int>(5));
    auto doubled = ok.transform([](std::optional<int> const& o) { return o ? (*o) * 2 : 0; });
    ASSERT_TRUE(doubled.has_value());
    EXPECT_EQ(*doubled, 10);
}

TEST(ExpectedStdTypes, UniquePtrRvalueMoveOut)
{
    ts::expected<std::unique_ptr<int>, std::string> ok(std::make_unique<int>(11));
    auto ptr = std::move(ok).value_or(std::unique_ptr<int>());
    ASSERT_TRUE(static_cast<bool>(ptr));
    EXPECT_EQ(*ptr, 11);
}

TEST(ExpectedStdTypes, PairAndArrayConstUsage)
{
    ts::expected<std::pair<int, int>, std::string> const ok({2, 3});
    ASSERT_TRUE(ok.has_value());
    // const& value()
    auto const& p = ok.value();
    EXPECT_EQ(p.first + p.second, 5);
    // const&& value()
    auto sum = std::move(ok).value().first + 3; // ok.value() const&& returns by value
    EXPECT_EQ(sum, 5);

    ts::expected<std::array<int, 3>, std::string> arr(std::array<int, 3>{1, 2, 3});
    ASSERT_TRUE(arr.has_value());
    EXPECT_EQ(arr->at(1), 2);
}

TEST(ExpectedStdTypes, TransformAndThenConstOverloads)
{
    ts::expected<int, std::string> const ok(21);
    auto doubled = ok.transform([](int v) { return v * 2; });
    ASSERT_TRUE(doubled.has_value());
    EXPECT_EQ(*doubled, 42);

    auto chained = ok.and_then([](int const v) { return ts::expected<int, std::string>(v + 1); });
    ASSERT_TRUE(chained.has_value());
    EXPECT_EQ(*chained, 22);
}

TEST(ExpectedStdTypes, ErrorStringTransformErrorConstRvalue)
{
    ts::expected<int, std::string> const bad(ts::unexpect, std::string("error"));
    auto mapped = std::move(bad).transform_error([](std::string const& s) { return s.size(); });
    ASSERT_FALSE(mapped.has_value());
    EXPECT_EQ(mapped.error(), 5U);
}

TEST(ExpectedStringTest, StringValueOr)
{
    ts::expected<std::string, std::error_code> ok(std::string("hi"));
    ts::expected<std::string, std::error_code> bad(ts::unexpected(std::make_error_code(std::errc::invalid_argument)));
    EXPECT_EQ(ok.value_or("x"), std::string("hi"));
    EXPECT_EQ(bad.value_or("x"), std::string("x"));
}

TEST(ExpectedStringTest, AndThenProducesSize)
{
    ts::expected<std::string, std::error_code> ok(std::string("hello"));
    auto result = ok.and_then([](std::string& s) { return ts::expected<std::size_t, std::error_code>(s.size()); });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 5u);
}

TEST(ExpectedStringTest, OrElseProvidesFallback)
{
    ts::expected<std::string, std::error_code> bad(ts::unexpected(std::make_error_code(std::errc::invalid_argument)));
    auto result = bad.or_else(
        [](std::error_code) { return ts::expected<std::string, std::error_code>(std::string("fallback")); });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string("fallback"));
}

TEST(ExpectedStringErrorTest, ErrorIsStringBasic)
{
    ts::expected<int, std::string> e(ts::unexpect, std::string("oops"));
    ASSERT_FALSE(e.has_value());
    EXPECT_EQ(e.error(), std::string("oops"));
}

TEST(ExpectedStringErrorTest, TransformErrorMapsStringToSize)
{
    ts::expected<int, std::string> e(ts::unexpect, std::string("oops"));
    auto mapped = e.transform_error([](std::string const& s) { return s.size(); });
    ASSERT_FALSE(mapped.has_value());
    EXPECT_EQ(mapped.error(), 4u);
}

TEST(ExpectedStringErrorTest, VoidWithStringErrorOrElse)
{
    ts::expected<void, std::string> bad(ts::unexpect, std::string("oops"));
    ASSERT_FALSE(bad.has_value());
    auto fixed = bad.or_else([](std::string&) { return ts::expected<void, std::string>(); });
    EXPECT_TRUE(fixed.has_value());
}

TEST(ExpectedStringTest, EqualityWithStringValues)
{
    ts::expected<std::string, std::error_code> ok1(std::string("a"));
    ts::expected<std::string, std::error_code> ok2(std::string("a"));
    ts::expected<std::string, std::error_code> ok3(std::string("b"));
    auto bad =
        ts::expected<std::string, std::error_code>(ts::unexpected(std::make_error_code(std::errc::invalid_argument)));

    EXPECT_TRUE(ok1 == ok2);
    EXPECT_FALSE(ok1 != ok2);
    EXPECT_FALSE(ok1 == ok3);
    EXPECT_TRUE(ok1 != ok3);
    EXPECT_FALSE(ok1 == bad);
    EXPECT_TRUE(ok1 != bad);
}

TEST(ExpectedStringTest, ValueOrRvalue)
{
    ts::expected<std::string, std::error_code> ok(std::string("foo"));
    EXPECT_EQ(std::move(ok).value_or("x"), std::string("foo"));

    ts::expected<std::string, std::error_code> bad(ts::unexpect, std::make_error_code(std::errc::invalid_argument));
    EXPECT_EQ(std::move(bad).value_or("x"), std::string("x"));
}
