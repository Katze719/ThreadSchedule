#include "../include/threadschedule/reflect.hpp"
#include <cassert>
#include <iostream>
#include <string>

// One macro: struct + reflection. Members written once as (Type, name).
THREADSCHEDULE_DEFINE_STRUCT_REFLECTION(Point, (int, x), (int, y));

THREADSCHEDULE_DEFINE_STRUCT_REFLECTION(Config, (std::string, name), (int, threads), (bool, enabled));

THREADSCHEDULE_DEFINE_STRUCT_REFLECTION(Single, (int, only));

// With methods: member list once, methods in THREADSCHEDULE_BODY(...).
THREADSCHEDULE_DEFINE_STRUCT_WITH_METHODS(PointWithMethods, 2, (int, x), (int, y), THREADSCHEDULE_BODY(
    void scale(int s)
    {
        x *= s;
        y *= s;
    }
    int sum() const { return x + y; }
))

int main()
{
    // member_count
    static_assert(threadschedule::reflect::member_count_v<Point> == 2);
    static_assert(threadschedule::reflect::member_count_v<Config> == 3);
    static_assert(threadschedule::reflect::member_count_v<Single> == 1);
    static_assert(threadschedule::reflect::member_count_v<int> == 0);

    // get_member_name
    assert(std::string(threadschedule::reflect::get_member_name<Point, 0>()) == "x");
    assert(std::string(threadschedule::reflect::get_member_name<Point, 1>()) == "y");
    assert(std::string(threadschedule::reflect::get_member_name<Config, 1>()) == "threads");

    // for_each_member
    Point p{3, 4};
    int count = 0;
    threadschedule::reflect::for_each_member(p, [&count](char const* name, auto& value) {
        (void)value;
        if (count == 0)
            assert(std::string(name) == "x");
        else
            assert(std::string(name) == "y");
        ++count;
    });
    assert(count == 2);

    Config c{"test", 8, false};
    count = 0;
    threadschedule::reflect::for_each_member(c, [&count](char const* n, auto& v) {
        if (count == 0)
        {
            assert(std::string(n) == "name");
            assert(std::string(v) == "test");
        }
        else if (count == 1)
        {
            assert(std::string(n) == "threads");
            assert(v == 8);
        }
        else
        {
            assert(std::string(n) == "enabled");
            assert(v == false);
        }
        ++count;
    });
    assert(count == 3);

    // Struct with methods
    PointWithMethods pm{10, 20};
    assert(pm.sum() == 30);
    pm.scale(2);
    assert(pm.x == 20 && pm.y == 40);
    static_assert(threadschedule::reflect::member_count_v<PointWithMethods> == 2);

    std::cout << "reflect_test passed\n";
    return 0;
}
