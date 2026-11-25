# Migrating from ThreadSchedule 3.x to 4.0.0

ThreadSchedule 4.0.0 requires C++17 or newer. The examples below compare
3.0.1 and 4.0.0 and use these common includes and namespace alias.

```cpp
#include <cassert>
#include <threadschedule/expected.hpp>
#include <utility>

namespace ts = threadschedule;
```

## Update your dependency

**v3**

```cmake
find_package(ThreadSchedule 3 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE ThreadSchedule::ThreadSchedule)
```

**v4**

```cmake
find_package(ThreadSchedule 4 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE ThreadSchedule::ThreadSchedule)
```

With FetchContent, change `GIT_TAG v3.0.1` to `GIT_TAG v4.0.0`. With Conan,
change `threadschedule/3.0.1` to `threadschedule/4.0.0`. Link the optional shared
registry with `ThreadSchedule::Runtime`.

## Update `expected` call sites

### Store `unexpect` explicitly as a value

For example, these types let v3 implicitly wrap `unexpect` as a value.

```cpp
struct tag_value
{
  tag_value(ts::unexpect_t) {}
};

struct tag_error
{
  explicit tag_error(int) {}
};

using tag_result = ts::expected<tag_value, tag_error>;
```

**v3**

```cpp
tag_result stored = ts::unexpect;
```

**v4**

```cpp
tag_result stored(std::in_place, ts::unexpect);
```

Use `tag_result(ts::unexpect, 7)` to construct the error alternative.

### Use comparison operators directly

With C++20 or newer, v3 allowed calls that supplied a template argument
explicitly. In v4, use the operator syntax, which also works in C++17.

**v3**

```cpp
constexpr ts::expected<long, int> result = 42L;
const bool equal = operator== <int>(result, 42);
const bool different = operator!= <int>(result, 7);
```

**v4**

```cpp
constexpr ts::expected<long, int> result = 42L;
const bool equal = result == 42;
const bool different = result != 7;
```

The underlying comparisons required by the selected overload must yield a
result implicitly convertible to `bool`.

### Converting to `expected<bool, E>`

The same direct construction can produce different results. In v3 it could
store the source's success status as a Boolean value. In v4 it preserves the
active alternative and converts only a successful value.

**v3**

```cpp
const ts::expected<int, int> zero = 0;
const ts::expected<int, int> failed(ts::unexpect, 7);
const ts::expected<bool, int> flag(zero);
const ts::expected<bool, int> from_error(failed);

assert(flag.has_value() && *flag);
assert(from_error.has_value() && !*from_error);
```

**v4**

```cpp
const ts::expected<int, int> zero = 0;
const ts::expected<int, int> failed(ts::unexpect, 7);
const ts::expected<bool, int> flag(zero);
const ts::expected<bool, int> from_error(failed);

assert(flag.has_value() && !*flag);
assert(from_error.has_error() && from_error.error() == 7);
```

If you want to store the success status, pass it explicitly with
`ts::expected<bool, int>(source.has_value())`.

### Check the active alternative before access

The v4 `has_error()` observer is shorthand for `!has_value()`. In v4,
calling `error()` on a successful `expected<void, E>` or dereferencing one
that holds an error terminates the program, including in release builds.

**v3**

```cpp
int
finish(ts::expected<void, int> const& operation)
{
  if (!operation.has_value())
    return operation.error();
  operation.value();
  return 0;
}
```

**v4**

```cpp
int
finish(ts::expected<void, int> const& operation)
{
  if (operation.has_error())
    return operation.error();
  *operation;
  return 0;
}
```

## Rebuild consumers of a shared C++ ABI

Rebuild the executable and shared libraries that exchange ThreadSchedule
objects with identical headers. Comparison template symbols changed, so
rebuild libraries that export those instantiations too.

Some copy assignments became trivial and `noexcept`. If a public signature
uses the exact member pointer type, it can change as in this example.

**v3**

```cpp
using result = ts::expected<int, int>;
using copy_assignment = result& (result::*)(result const&);
void register_assignment(copy_assignment);
```

**v4**

```cpp
using result = ts::expected<int, int>;
using copy_assignment = result& (result::*)(result const&) noexcept;
void register_assignment(copy_assignment);
```

For shared libraries built with different GCC versions or language modes,
match the toolchain's ABI settings and test the exported types you ship.
See the [compatibility and ABI guide](COMPATIBILITY.md).
