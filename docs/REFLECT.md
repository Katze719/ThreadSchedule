# Struct Reflection (C++17+)

Define a struct and its reflection in one place; each member is written once as `(Type, name)`. Include `#include <threadschedule/reflect.hpp>` only where needed.

## Quick Start

**Data-only struct:**

```cpp
THREADSCHEDULE_DEFINE_STRUCT_REFLECTION(Point, (int, x), (int, y));

Point p{3, 4};
threadschedule::reflect::for_each_member(p, [](char const* name, auto& value) {
    std::cout << name << " = " << value << "\n";
});
```

**Struct with methods** – member list once, method block in `THREADSCHEDULE_BODY(...)`. Second argument = number of members.

```cpp
THREADSCHEDULE_DEFINE_STRUCT_WITH_METHODS(Point, 2, (int, x), (int, y), THREADSCHEDULE_BODY(
    void scale(int s) { x *= s; y *= s; }
    int sum() const { return x + y; }
))
```

1–8 members per type. Alternative: `THREADSCHEDULE_DEFINE_STRUCT_BEGIN` / `THREADSCHEDULE_DEFINE_STRUCT_END` (member list in both), or `#define POINT_MEMBERS (int,x), (int,y)` and use it in both.

## API

- **`reflect::for_each_member(obj, f)`** – `f(char const* name, MemberType& value)` for each member.
- **`reflect::member_count_v<T>`** – number of reflected members (0 if not defined with the macros).
- **`reflect::get_member_name<T, I>()`** – name of the I-th member (0-based).

Use cases: pretty-print, serialization (e.g. JSON keys), validation. Header-only; no ABI impact.
