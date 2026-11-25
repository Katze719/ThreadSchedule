#include <array>
#include <gtest/gtest.h>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <threadschedule/expected.hpp>
#include <type_traits>
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

struct nondefault_error
{
  nondefault_error() = delete;
  explicit nondefault_error(int value) : value(value) {}
  int value;
};

struct throwing_copy_value
{
  static bool throw_on_copy;

  explicit throwing_copy_value(int value = 0) : value(value) {}
  throwing_copy_value(throwing_copy_value const& other) : value(other.value)
  {
    if (throw_on_copy)
      throw std::runtime_error("copy failed");
  }
  throwing_copy_value(throwing_copy_value&&) noexcept = default;
  auto operator=(throwing_copy_value const&) -> throwing_copy_value& = default;
  auto operator=(throwing_copy_value&&) noexcept -> throwing_copy_value& = default;

  int value;
};

bool throwing_copy_value::throw_on_copy = false;

struct list_payload
{
  constexpr list_payload(std::initializer_list<int>& items, int extra) noexcept : count(items.size()), extra(extra) {}

  std::size_t count;
  int extra;
};

struct immovable_payload
{
  constexpr explicit immovable_payload(int value) : value(value) {}
  immovable_payload(immovable_payload const&) = delete;
  immovable_payload(immovable_payload&&) = delete;
  auto operator=(immovable_payload const&) -> immovable_payload& = delete;
  auto operator=(immovable_payload&&) -> immovable_payload& = delete;
  ~immovable_payload() = default;
  int value;
};

struct copy_only
{
  copy_only() = default;
  copy_only(copy_only const&) = default;
  copy_only(copy_only&&) = delete;
  auto operator=(copy_only const&) -> copy_only& = default;
  auto operator=(copy_only&&) -> copy_only& = delete;
  ~copy_only() = default;
};

struct constexpr_may_throw
{
  constexpr explicit constexpr_may_throw(int value) noexcept(false) : value(value) {}
  constexpr constexpr_may_throw(constexpr_may_throw const&) noexcept(false) = default;
  constexpr constexpr_may_throw(constexpr_may_throw&& other) noexcept(false) : value(other.value) {}
  auto operator=(constexpr_may_throw const&) -> constexpr_may_throw& = default;
  auto operator=(constexpr_may_throw&&) -> constexpr_may_throw& = default;
  ~constexpr_may_throw() = default;

  constexpr auto
  operator=(int input) noexcept -> constexpr_may_throw&
  {
    value = input;
    return *this;
  }

  int value;
};

struct throwing_move_error
{
  explicit throwing_move_error(int code) : code(code) {}
  throwing_move_error(throwing_move_error const&) = default;
  throwing_move_error(throwing_move_error&&)
  {
    throw std::runtime_error("move failed");
  }
  auto operator=(throwing_move_error const&) -> throwing_move_error& = default;
  auto operator=(throwing_move_error&&) -> throwing_move_error& = default;
  ~throwing_move_error() = default;
  int code;
};

template <typename Result, typename F, typename = void>
inline constexpr bool can_and_then_v = false;
template <typename Result, typename F>
inline constexpr bool
    can_and_then_v<Result, F, std::void_t<decltype(std::declval<Result>().and_then(std::declval<F>()))>> = true;

template <typename Result, typename = void>
inline constexpr bool has_member_swap_v = false;
template <typename Result>
inline constexpr bool
    has_member_swap_v<Result, std::void_t<decltype(std::declval<Result&>().swap(std::declval<Result&>()))>> = true;

ts::expected<int, std::error_code>
parse_int_ok()
{
  return 42;
}

ts::expected<int, std::error_code>
parse_int_fail()
{
  return ts::unexpected(std::make_error_code(std::errc::invalid_argument));
}

ts::expected<void, std::error_code>
do_void_ok()
{
  return {};
}

ts::expected<void, std::error_code>
do_void_fail()
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

TEST(ExpectedTest, ConvertingToBoolPreservesValueOrError)
{
  constexpr ts::expected<int, int> zero(0);
  constexpr ts::expected<int, int> nonzero(7);
  constexpr ts::expected<int, int> failure(ts::unexpect, 9);

  constexpr ts::expected<bool, long> false_value = zero;
  constexpr ts::expected<bool, long> true_value = nonzero;
  constexpr ts::expected<bool, long> error = failure;
  static_assert(false_value.has_value() && !*false_value);
  static_assert(true_value.has_value() && *true_value);
  static_assert(!error.has_value() && error.error() == 9);

  const ts::expected<bool, int> boolean(false);
  const ts::expected<const bool, long> copied(boolean);
  ASSERT_TRUE(copied.has_value());
  EXPECT_FALSE(*copied);
}

TEST(ExpectedTest, ConvertingConstructorsRespectExplicitAndNestedValues)
{
  using source = ts::expected<int, int>;
  static_assert(std::is_convertible_v<source, ts::expected<long, long>>);
  static_assert(std::is_constructible_v<ts::expected<long, nondefault_error>, source>);
  static_assert(!std::is_convertible_v<source, ts::expected<long, nondefault_error>>);
  static_assert(!std::is_constructible_v<ts::expected<bool, int>, ts::expected<void, int>>);

  const source inner(ts::unexpect, 13);
  const ts::expected<source, int> outer(inner);
  ASSERT_TRUE(outer.has_value());
  ASSERT_FALSE(outer->has_value());
  EXPECT_EQ(outer->error(), 13);
}

TEST(ExpectedTest, ConvertingFromRvalueMovesError)
{
  using source = ts::expected<int, std::unique_ptr<int>>;
  using destination = ts::expected<bool, std::unique_ptr<const int>>;
  static_assert(!std::is_constructible_v<destination, source const&>);
  auto failed = source(ts::unexpect, std::make_unique<int>(42));
  const destination converted = std::move(failed);
  ASSERT_FALSE(converted.has_value());
  ASSERT_TRUE(converted.error());
  EXPECT_EQ(*converted.error(), 42);
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

  ts::expected<Point, std::error_code> p(Point{ 3, 4 });
  EXPECT_EQ(p->x, 3);
  EXPECT_EQ(p->y, 4);

  ts::expected<Point, std::error_code> const cp(Point{ 5, 6 });
  EXPECT_EQ(cp->x, 5);
  EXPECT_EQ(cp->y, 6);
}

TEST(ExpectedTest, OperatorArrowIgnoresOverloadedAddressOf)
{
  struct value_type
  {
    int value;

    auto
    operator&() noexcept -> value_type*
    {
      return nullptr;
    }
    auto
    operator&() const noexcept -> value_type const*
    {
      return nullptr;
    }
  };

  ts::expected<value_type> value(value_type{ 7 });
  ASSERT_EQ(value.operator->(), std::addressof(*value));
  EXPECT_EQ(value->value, 7);

  auto const& const_value = value;
  ASSERT_EQ(const_value.operator->(), std::addressof(*const_value));
  EXPECT_EQ(const_value->value, 7);
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

TEST(ExpectedTest, ConstexprValue)
{
  constexpr auto observe = []
    {
      ts::expected<int, int> value(17);
      const ts::expected<int, int> const_value(21);
      ts::expected<void, int> empty;
      empty.value();
      const auto read = value.value() + const_value.value();
      return read + std::move(value).value() + std::move(const_value).value();
    };
  static_assert(observe() == 76);
}

TEST(ExpectedTest, ErrorOr)
{
  constexpr ts::expected<int, long> value(42);
  constexpr ts::expected<int, long> error(ts::unexpect, 7);
  constexpr ts::expected<void, long> empty;
  constexpr ts::expected<void, long> void_error(ts::unexpect, 8);
  static_assert(value.error_or(9) == 9);
  static_assert(error.error_or(9) == 7);
  static_assert(empty.error_or(9) == 9);
  static_assert(void_error.error_or(9) == 8);
  static_assert(ts::expected<int, long>(42).error_or(9) == 9);
  static_assert(ts::expected<void, long>(ts::unexpect, 8).error_or(9) == 8);
}

TEST(ExpectedTest, ErrorOrMovesErrorOrFallback)
{
  auto result = ts::expected<int, std::unique_ptr<int>>(ts::unexpect, std::make_unique<int>(42));
  auto unused_fallback = std::make_unique<int>(7);
  const auto error = std::move(result).error_or(std::move(unused_fallback));
  ASSERT_TRUE(error);
  EXPECT_EQ(*error, 42);
  EXPECT_TRUE(unused_fallback);

  auto empty = ts::expected<void, std::unique_ptr<int>>();
  auto fallback = std::make_unique<int>(9);
  const auto recovered = std::move(empty).error_or(std::move(fallback));
  ASSERT_TRUE(recovered);
  EXPECT_EQ(*recovered, 9);
  EXPECT_FALSE(fallback);
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

TEST(ExpectedTest, TransformSupportsVoidResults)
{
  int calls = 0;
  ts::expected<int, std::error_code> value(42);
  auto from_value = value.transform([&calls](int) { ++calls; });
  static_assert(std::is_same_v<decltype(from_value), ts::expected<void, std::error_code>>);
  EXPECT_TRUE(from_value.has_value());

  ts::expected<void, std::error_code> empty;
  auto from_void = empty.transform([&calls] { ++calls; });
  static_assert(std::is_same_v<decltype(from_void), ts::expected<void, std::error_code>>);
  EXPECT_TRUE(from_void.has_value());
  EXPECT_EQ(calls, 2);
}

TEST(ExpectedTest, CopyTraitsFollowStoredTypes)
{
  using move_only = ts::expected<std::unique_ptr<int>, std::error_code>;
  static_assert(!std::is_copy_constructible_v<move_only>);
  static_assert(!std::is_copy_assignable_v<move_only>);
  static_assert(std::is_move_constructible_v<move_only>);
  static_assert(std::is_move_assignable_v<move_only>);
}

TEST(ExpectedTest, VoidSuccessDoesNotConstructError)
{
  ts::expected<void, nondefault_error> result;
  EXPECT_TRUE(result.has_value());
}

TEST(ExpectedTest, ThrowingCrossStateAssignmentPreservesOriginalState)
{
  ts::expected<throwing_copy_value, int> source(std::in_place, 42);
  ts::expected<throwing_copy_value, int> destination(ts::unexpect, 7);

  throwing_copy_value::throw_on_copy = true;
  EXPECT_THROW(destination = source, std::runtime_error);
  throwing_copy_value::throw_on_copy = false;

  ASSERT_FALSE(destination.has_value());
  EXPECT_EQ(destination.error(), 7);
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
    Complex(int x, double y) : a(x), b(y) {}
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
  ts::expected<std::vector<int>, std::string> ok(std::vector<int>{ 1, 2, 3 });
  ASSERT_TRUE(ok.has_value());
  auto sizes = ok.and_then([](std::vector<int>& v) { return ts::expected<std::size_t, std::string>(v.size()); });
  ASSERT_TRUE(sizes.has_value());
  EXPECT_EQ(*sizes, 3U);
}

TEST(ExpectedStdTypes, VectorOrElseFallback)
{
  ts::expected<std::vector<int>, std::string> bad(ts::unexpect, std::string("err"));
  auto res
      = bad.or_else([](std::string) { return ts::expected<std::vector<int>, std::string>(std::vector<int>{ 7 }); });
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
  ts::expected<std::pair<int, int>, std::string> const ok({ 2, 3 });
  ASSERT_TRUE(ok.has_value());
  // const& value()
  auto const& p = ok.value();
  EXPECT_EQ(p.first + p.second, 5);
  // const&& value()
  auto sum = std::move(ok).value().first + 3; // ok.value() const&& returns by value
  EXPECT_EQ(sum, 5);

  ts::expected<std::array<int, 3>, std::string> arr(std::array<int, 3>{ 1, 2, 3 });
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
  auto result = bad.or_else([](std::error_code)
                              { return ts::expected<std::string, std::error_code>(std::string("fallback")); });
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
  auto bad
      = ts::expected<std::string, std::error_code>(ts::unexpected(std::make_error_code(std::errc::invalid_argument)));

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

TEST(ExpectedConformanceTest, UnexpectedConversionsAndSwap)
{
  constexpr ts::unexpected<short> small(short{ 5 });
  constexpr auto larger = ts::unexpected(5L);
  static_assert(small == larger && larger == small);
  static_assert(small != ts::unexpected(8));

  constexpr ts::unexpected<list_payload> listed(std::in_place, { 1, 2 }, 3);
  static_assert(listed.error().count == 2 && listed.error().extra == 3);
  constexpr ts::unexpected<list_payload> in_place(std::in_place, std::initializer_list<int>{}, 7);
  static_assert(in_place.error().count == 0 && in_place.error().extra == 7);

  static_assert(std::is_convertible_v<decltype(small), ts::expected<int, long>>);
  static_assert(std::is_constructible_v<ts::expected<int, nondefault_error>, decltype(small)>);
  static_assert(!std::is_convertible_v<decltype(small), ts::expected<int, nondefault_error>>);
  static_assert(std::is_constructible_v<ts::expected<void, nondefault_error>, decltype(small)>);
  static_assert(!std::is_convertible_v<decltype(small), ts::expected<void, nondefault_error>>);
  static_assert(
      !std::is_constructible_v<ts::expected<int, std::unique_ptr<int>>, ts::unexpected<std::unique_ptr<int>> const&>);
  const ts::expected<int, long> error = small;
  ASSERT_FALSE(error.has_value());
  EXPECT_EQ(error.error(), 5);

  auto first = ts::unexpected(7);
  auto second = ts::unexpected(9);
  static_assert(noexcept(swap(first, second)));
  using std::swap;
  swap(first, second);
  EXPECT_EQ(first.error(), 9);
  EXPECT_EQ(second.error(), 7);
}

TEST(ExpectedConformanceTest, InitializerListConstructionAndEmplace)
{
  using result_type = ts::expected<list_payload, list_payload>;
  constexpr result_type value(std::in_place, { 1, 2, 3 }, 4);
  constexpr result_type error(ts::unexpect, { 1, 2 }, 5);
  constexpr ts::expected<void, list_payload> void_error(ts::unexpect, { 1 }, 6);
  static_assert(value->count == 3 && value->extra == 4);
  static_assert(error.error().count == 2 && error.error().extra == 5);
  static_assert(void_error.error().count == 1 && void_error.error().extra == 6);
  static_assert(noexcept(std::declval<result_type&>().emplace({ 1, 2 }, 4)));

  result_type emplaced = error;
  auto& item = emplaced.emplace({ 1, 2, 3, 4 }, 7);
  ASSERT_TRUE(emplaced.has_value());
  EXPECT_EQ(std::addressof(item), emplaced.operator->());
  EXPECT_EQ(item.count, 4);
  EXPECT_EQ(item.extra, 7);
}

TEST(ExpectedConformanceTest, CvVoid)
{
  using result_type = ts::expected<const volatile void, long>;
  static_assert(std::is_same_v<result_type::value_type, const volatile void>);
  static_assert(std::is_same_v<result_type::rebind<int>, ts::expected<int, long>>);
  static_assert(std::is_same_v<ts::expected<int, long>::rebind<const void>, ts::expected<const void, long>>);
  static_assert(std::is_nothrow_default_constructible_v<result_type>);
  static_assert(std::is_nothrow_constructible_v<result_type, std::in_place_t>);
  static_assert(!std::is_convertible_v<std::in_place_t, result_type>);
  static_assert(noexcept(*std::declval<result_type&>()));
  static_assert(std::is_same_v<void, decltype(*std::declval<result_type const&>())>);
  static_assert(!std::is_constructible_v<result_type, ts::expected<int, long>>);

  constexpr result_type success(std::in_place);
  constexpr ts::expected<const void, int> source(ts::unexpect, 6);
  constexpr result_type failure = source;
  static_assert(success == ts::expected<volatile void, short>());
  static_assert(failure == source && source == failure);
  static_assert(success != failure);
  static_assert(failure.error() == 6);
  *success;
  result_type assigned = success;
  static_assert(std::is_same_v<decltype(assigned = ts::unexpected(11)), result_type&>);
  assigned = ts::unexpected(11);
  ASSERT_FALSE(assigned.has_value());
  EXPECT_EQ(assigned.error(), 11);
  assigned.emplace();
  EXPECT_TRUE(assigned.has_value());
}

TEST(ExpectedConformanceTest, VoidRvalueValueThrowsMoveOnlyError)
{
  using error_type = std::unique_ptr<int>;
  auto success = ts::expected<const void, error_type>(std::in_place);
  EXPECT_NO_THROW(std::move(success).value());
  auto error = ts::expected<void, error_type>(ts::unexpect, std::make_unique<int>(23));
  try
    {
      std::move(error).value();
      FAIL() << "Expected bad_expected_access";
    }
  catch (ts::bad_expected_access<error_type> const& caught)
    {
      ASSERT_TRUE(caught.error());
      EXPECT_EQ(*caught.error(), 23);
    }
  EXPECT_FALSE(error.error());
}

TEST(ExpectedConformanceTest, MoveOnlyPropagationConstrainsAndThen)
{
  using result_type = ts::expected<int, std::unique_ptr<int>>;
  auto produce = [](int value) { return result_type(value + 1); };
  static_assert(!can_and_then_v<result_type&, decltype(produce)>);
  static_assert(!can_and_then_v<result_type const&, decltype(produce)>);
  static_assert(!can_and_then_v<result_type const&&, decltype(produce)>);
  static_assert(can_and_then_v<result_type&&, decltype(produce)>);
  auto failed = result_type(ts::unexpect, std::make_unique<int>(13));
  auto& source_error = failed.error();
  auto propagated = std::move(failed).and_then(produce);
  ASSERT_FALSE(propagated.has_value());
  ASSERT_TRUE(propagated.error());
  EXPECT_EQ(*propagated.error(), 13);
  EXPECT_FALSE(source_error);
}

TEST(ExpectedConformanceTest, MonadicOperationsPreserveTheSpecifiedTypes)
{
  const auto returned = ts::expected<long, int>(15);
  ts::expected<int, int> value(4);
  auto next = value.and_then([&returned](int&) -> ts::expected<long, int> const& { return returned; });
  static_assert(std::is_same_v<decltype(next), ts::expected<long, int>>);
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(*next, 15);

  const auto fallback = ts::expected<int, long>(21);
  auto recover = [&fallback](int) -> ts::expected<int, long> const& { return fallback; };
  auto recovered = ts::expected<int, int>(ts::unexpect, 2).or_else(recover);
  static_assert(std::is_same_v<decltype(recovered), ts::expected<int, long>>);
  ASSERT_TRUE(recovered.has_value());
  EXPECT_EQ(*recovered, 21);
  EXPECT_EQ(value.or_else(recover).value(), 4);

  ts::expected<const void, int> empty;
  auto mapped = empty.transform_error([](int error) { return static_cast<long>(error); });
  static_assert(std::is_same_v<decltype(mapped), ts::expected<const void, long>>);
  EXPECT_TRUE(mapped.has_value());
}

TEST(ExpectedConformanceTest, TransformConstructsImmovablePayloadsInPlace)
{
  auto value = ts::expected<int, int>(6).transform([](int input) { return immovable_payload(input * 2); });
  static_assert(std::is_same_v<decltype(value), ts::expected<immovable_payload, int>>);
  static_assert(!std::is_move_constructible_v<decltype(value)>);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value->value, 12);

  auto without_value = ts::expected<void, int>().transform([] { return immovable_payload(11); });
  ASSERT_TRUE(without_value.has_value());
  EXPECT_EQ(without_value->value, 11);

  auto error
      = ts::expected<int, int>(ts::unexpect, 7).transform_error([](int input) { return immovable_payload(input * 2); });
  static_assert(std::is_same_v<decltype(error), ts::expected<int, immovable_payload>>);
  ASSERT_FALSE(error.has_value());
  EXPECT_EQ(error.error().value, 14);

  auto void_error = ts::expected<void, int>(ts::unexpect, 8)
                        .transform_error([](int input) { return immovable_payload(input * 2); });
  ASSERT_FALSE(void_error.has_value());
  EXPECT_EQ(void_error.error().value, 16);
}

TEST(ExpectedConformanceTest, VoidAssignmentAndSwapRollBackOnThrow)
{
  using result_type = ts::expected<void, throwing_move_error>;
  static_assert(std::is_move_assignable_v<result_type>);
  static_assert(!std::is_nothrow_move_assignable_v<result_type>);
  static_assert(has_member_swap_v<result_type>);
  static_assert(!noexcept(std::declval<result_type&>().swap(std::declval<result_type&>())));
  static_assert(!has_member_swap_v<ts::expected<throwing_move_error, throwing_move_error>>);

  result_type success;
  result_type error(ts::unexpect, 17);
  auto& source = error;
  EXPECT_THROW(success = std::move(source), std::runtime_error);
  EXPECT_TRUE(success.has_value());
  ASSERT_FALSE(error.has_value());
  EXPECT_EQ(error.error().code, 17);

  EXPECT_THROW(success.swap(error), std::runtime_error);
  EXPECT_TRUE(success.has_value());
  ASSERT_FALSE(error.has_value());
  EXPECT_EQ(error.error().code, 17);

  ts::expected<throwing_move_error, int> value(std::in_place, 19);
  ts::expected<throwing_move_error, int> other(ts::unexpect, 21);
  EXPECT_THROW(value.swap(other), std::runtime_error);
  ASSERT_TRUE(value.has_value());
  ASSERT_FALSE(other.has_value());
  EXPECT_EQ(value->code, 19);
  EXPECT_EQ(other.error(), 21);
}

TEST(ExpectedConformanceTest, RvalueConstructionFallsBackToCopyWhenMoveCannotParticipate)
{
  using result_type = ts::expected<copy_only, std::optional<int>>;
  static_assert(std::is_copy_constructible_v<result_type>);
  static_assert(std::is_move_constructible_v<result_type>);
  static_assert(!std::is_move_constructible_v<ts::expected<copy_only, std::unique_ptr<int>>>);

  using observable_error = ts::expected<copy_only, std::string>;
  observable_error original(ts::unexpect, "copied error");
  auto const& original_error = original.error();
  observable_error converted(std::move(original));
  ASSERT_FALSE(converted.has_value());
  EXPECT_EQ(converted.error(), "copied error");
  EXPECT_EQ(original_error, converted.error());
}

#if defined(__cpp_lib_variant) && __cpp_lib_variant >= 202106L
TEST(ExpectedConformanceTest, PotentiallyThrowingTransitionCanBeConstantEvaluated)
{
  constexpr auto transition = []
    {
      ts::expected<constexpr_may_throw, int> nonvoid(ts::unexpect, 4);
      nonvoid = 8;
      ts::expected<void, constexpr_may_throw> empty;
      empty = ts::unexpected(9);
      using std::swap;
      ts::expected<void, constexpr_may_throw> error;
      swap(empty, error);
      return nonvoid->value == 8 && empty.has_value() && error.error().value == 9;
    };
  static_assert(transition());
}
#endif

TEST(ExpectedConformanceTest, TrivialityAndNoexceptFollowTheStoredTypes)
{
  using result_type = ts::expected<int, long>;
  static_assert(std::is_trivially_copy_constructible_v<result_type>);
  static_assert(std::is_trivially_move_constructible_v<result_type>);
  static_assert(std::is_trivially_destructible_v<result_type>);
  static_assert(std::is_trivially_copy_constructible_v<ts::expected<void, int>>);
  static_assert(std::is_trivially_move_constructible_v<ts::expected<const void, int>>);
  static_assert(std::is_trivially_destructible_v<ts::expected<volatile void, int>>);
  static_assert(std::is_nothrow_move_constructible_v<result_type>);
  static_assert(std::is_nothrow_move_assignable_v<result_type>);
  static_assert(noexcept(ts::swap(std::declval<result_type&>(), std::declval<result_type&>())));
  static_assert(!std::is_trivially_destructible_v<ts::expected<void, std::string>>);
}

#if THREADSCHEDULE_HAS_STD_EXPECTED
TEST(ExpectedInteropTest, ImplicitlyConvertsCopyableStatesToStdExpected)
{
  static_assert(std::is_convertible_v<ts::expected<int, std::string> const&, std::expected<int, std::string>>);

  auto accept_standard = [](std::expected<int, std::string> value) { return value; };

  ts::expected<int, std::string> value(42);
  auto standard_value = accept_standard(value);
  ASSERT_TRUE(standard_value.has_value());
  EXPECT_EQ(standard_value.value(), 42);

  ts::expected<int, std::string> error(ts::unexpect, "failed");
  auto standard_error = accept_standard(error);
  ASSERT_FALSE(standard_error.has_value());
  EXPECT_EQ(standard_error.error(), "failed");

  ts::expected<void, std::string> empty;
  std::expected<void, std::string> standard_empty = empty;
  EXPECT_TRUE(standard_empty.has_value());
}

TEST(ExpectedInteropTest, MovesMoveOnlyStatesToStdExpected)
{
  using move_value = ts::expected<std::unique_ptr<int>, std::string>;
  static_assert(std::is_convertible_v<move_value&&, std::expected<std::unique_ptr<int>, std::string>>);
  static_assert(!std::is_convertible_v<move_value const&, std::expected<std::unique_ptr<int>, std::string>>);

  move_value value(std::make_unique<int>(42));
  std::expected<std::unique_ptr<int>, std::string> standard_value = std::move(value);
  ASSERT_TRUE(standard_value.has_value());
  ASSERT_TRUE(static_cast<bool>(standard_value.value()));
  EXPECT_EQ(*standard_value.value(), 42);

  ts::expected<void, std::unique_ptr<int>> error(ts::unexpect, std::make_unique<int>(7));
  std::expected<void, std::unique_ptr<int>> standard_error = std::move(error);
  ASSERT_FALSE(standard_error.has_value());
  ASSERT_TRUE(static_cast<bool>(standard_error.error()));
  EXPECT_EQ(*standard_error.error(), 7);
}
TEST(ExpectedInteropTest, ConformsToDraftConstructionAndObservers)
{
  constexpr auto check = [](auto standard, auto library)
    { return standard.has_value() == library.has_value() && standard.error_or(9) == library.error_or(9); };
#  if __cpp_lib_expected >= 202211L
  constexpr std::expected<const void, int> standard_empty(std::in_place);
  constexpr ts::expected<const void, int> library_empty(std::in_place);
  static_assert(check(standard_empty, library_empty));
  static_assert(check(std::expected<long, int>(std::unexpect, 4), ts::expected<long, int>(ts::unexpect, 4)));

  constexpr auto transform
      = [](auto input) { return input.transform([](int value) { return immovable_payload(value + 1); })->value; };
  static_assert(transform(std::expected<int, int>(4)) == transform(ts::expected<int, int>(4)));
  constexpr auto transform_error = [](auto input)
    { return input.transform_error([](int error) { return immovable_payload(error + 1); }).error().value; };
  static_assert(transform_error(std::expected<void, int>(std::unexpect, 7))
                == transform_error(ts::expected<void, int>(ts::unexpect, 7)));
#  endif
  static_assert(std::is_same_v<std::expected<const void, int>::value_type, ts::expected<const void, int>::value_type>);
  static_assert(std::is_nothrow_move_assignable_v<std::expected<int, int>>
                == std::is_nothrow_move_assignable_v<ts::expected<int, int>>);

#  if defined(__cpp_lib_variant) && __cpp_lib_variant >= 202106L
  constexpr auto transition = [](auto input, const auto& source)
    {
      input = source;
      return input.emplace({ 1, 2 }, 5).count;
    };
  static_assert(transition(std::expected<list_payload, int>(std::unexpect, 8),
                           std::expected<list_payload, int>(std::in_place, std::initializer_list<int>{}, 9))
                == transition(ts::expected<list_payload, int>(ts::unexpect, 8),
                              ts::expected<list_payload, int>(std::in_place, std::initializer_list<int>{}, 9)));
#  endif
}
#endif
