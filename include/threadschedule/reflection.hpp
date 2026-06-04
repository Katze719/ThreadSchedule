#pragma once

#if defined(THREADSCHEDULE_HAS_REFLECTION) && THREADSCHEDULE_HAS_REFLECTION

#include <cstddef>
#include <array>
#include <meta>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace threadschedule::reflect
{

using info = std::meta::info;
inline constexpr bool enabled = true;

template <typename T>
consteval auto fields() -> std::span<info const>
{
    static_assert(std::meta::is_class_type(^^T) || std::meta::is_union_type(^^T),
                  "threadschedule::reflect::fields<T>() requires a class or union type");
    return std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()));
}

template <typename T>
consteval auto field_count() -> std::size_t
{
    return fields<T>().size();
}

template <typename T, std::size_t I>
consteval auto field_info() -> info
{
    static_assert(I < field_count<T>(), "reflection field index out of range");
    return fields<T>()[I];
}

template <info Field>
consteval auto field_name() -> std::string_view
{
    return std::string_view(std::define_static_string(std::meta::identifier_of(Field)));
}

template <typename T, std::size_t I>
consteval auto field_name() -> std::string_view
{
    return field_name<field_info<T, I>()>();
}

template <typename T>
consteval auto type_name() -> std::string_view
{
    return std::string_view(std::define_static_string(std::meta::display_string_of(^^T)));
}

template <info Field>
using field_type_t = [: std::meta::type_of(Field) :];

template <info Field, typename T>
constexpr decltype(auto) get(T&& obj)
{
    return (std::forward<T>(obj).[:Field:]);
}

template <info Field, typename Owner>
inline constexpr bool is_field_of_v = std::meta::is_same_type(std::meta::parent_of(Field), ^^Owner);

template <typename T>
consteval auto field_names() -> std::span<char const* const>;

namespace detail
{

template <info... Fields>
struct projection_type;

template <info Field>
struct projection_type<Field>
{
    using type = field_type_t<Field>;
};

template <info First, info Second, info... Rest>
struct projection_type<First, Second, Rest...>
{
    using type = std::tuple<field_type_t<First>, field_type_t<Second>, field_type_t<Rest>...>;
};

template <typename T, typename F, std::size_t... I>
constexpr void visit_fields_impl(T&& obj, F&& fn, std::index_sequence<I...>)
{
    using object_type = std::remove_cv_t<std::remove_reference_t<T>>;
    constexpr auto names = field_names<object_type>();
    (fn(names[I], get<field_info<object_type, I>()>(std::forward<T>(obj))), ...);
}

template <typename T, std::size_t... I>
consteval auto field_names_impl(std::index_sequence<I...>) -> std::span<char const* const>
{
    return std::define_static_array(
        std::array<char const*, sizeof...(I)>{std::define_static_string(std::meta::identifier_of(field_info<T, I>()))...});
}

template <info First>
consteval info first_field() noexcept
{
    return First;
}

template <info Field, typename Owner>
consteval void require_field_owner()
{
    static_assert(std::meta::is_same_type(std::meta::parent_of(Field), ^^Owner),
                  "Reflection field does not belong to the requested owner type");
}

} // namespace detail

template <typename T>
consteval auto field_names() -> std::span<char const* const>
{
    return detail::field_names_impl<T>(std::make_index_sequence<field_count<T>()>{});
}

template <typename T, typename F>
constexpr void visit_fields(T&& obj, F&& fn)
{
    using object_type = std::remove_cv_t<std::remove_reference_t<T>>;
    detail::visit_fields_impl(std::forward<T>(obj), std::forward<F>(fn),
                              std::make_index_sequence<field_count<object_type>()>{});
}

template <info... Fields>
using projection_t = typename detail::projection_type<Fields...>::type;

template <info... Fields, typename T>
constexpr auto project_value(T&& obj) -> projection_t<Fields...>
{
    static_assert(sizeof...(Fields) > 0, "project_value requires at least one field");
    if constexpr (sizeof...(Fields) == 1)
    {
        return get<detail::first_field<Fields...>()>(std::forward<T>(obj));
    }
    else
    {
        return projection_t<Fields...>{get<Fields>(std::forward<T>(obj))...};
    }
}

template <info Field, typename Owner>
consteval void require_field_owner()
{
    detail::require_field_owner<Field, Owner>();
}

} // namespace threadschedule::reflect

#endif
