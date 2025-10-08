#include <gtest/gtest.h>
#include <system_error>
#include <threadschedule/expected.hpp>

using threadschedule::expected;
using threadschedule::unexpected;

using namespace threadschedule;

namespace
{

enum class parse_error
{
    invalid_input = 1,
    overflow = 2
};

expected<int, std::error_code> parse_int_ok()
{
    return 42;
}

expected<int, std::error_code> parse_int_fail()
{
    return threadschedule::unexpected(std::make_error_code(std::errc::invalid_argument));
}

expected<void, std::error_code> do_void_ok()
{
    return {};
}

expected<void, std::error_code> do_void_fail()
{
    return threadschedule::unexpected(std::make_error_code(std::errc::operation_not_permitted));
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
