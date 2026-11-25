# Migrating from ThreadSchedule 3.x to 4.0.0

ThreadSchedule 4.0.0 keeps C++17 as its minimum. The source and ABI changes
affect `threadschedule::expected` and its `result<T>` alias.

## Update your dependency

Use `v4.0.0` as the FetchContent `GIT_TAG`, or request major version 4 from
an installed CMake package.

```cmake
find_package(ThreadSchedule 4 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE ThreadSchedule::ThreadSchedule)
```

The optional shared registry is available through `ThreadSchedule::Runtime`.
Conan consumers can use the `threadschedule/4.0.0` recipe.

## Update `expected` call sites

### Store `unexpect` explicitly as a value

To wrap `unexpect` in a value type that can be constructed from
`threadschedule::unexpect_t`, use the in-place constructor.

```cpp
#include <system_error>
#include <threadschedule/expected.hpp>
#include <utility>

struct tag_value
{
  explicit tag_value(threadschedule::unexpect_t) {}
};

threadschedule::expected<tag_value, std::error_code> value(std::in_place, threadschedule::unexpect);
```

Use `expected(threadschedule::unexpect, ...)` to construct the error
alternative.

### Use comparison operators directly

Replace explicit calls such as `operator==<int>(result, 42)` with
`result == 42`. Use the same operator syntax for `!=`. The comparisons
required for the chosen overload must yield a result implicitly convertible
to `bool`.

### Check which alternative is active

When converting to `expected<bool, E>`, a successful zero produces a stored
`false` while `has_value()` still returns `true`. A source error remains an
error.

Check `has_error()` before reading `error()` and `has_value()` before
dereferencing. Invalid state access through `error()` or `operator*` now
terminates for `expected<void, E>` in release builds too.

## Rebuild consumers of a shared C++ ABI

Rebuild the executable and shared libraries that exchange ThreadSchedule
objects with identical headers. Some copy assignments are now trivial and
`noexcept`, so review exported signatures that contain pointers to affected
assignment operators. Comparison template symbols also changed, so rebuild
libraries that export those instantiations.

For shared libraries built with different GCC versions or language modes,
match the toolchain's ABI settings and test the exported types you ship.
See the [compatibility and ABI guide](COMPATIBILITY.md).
