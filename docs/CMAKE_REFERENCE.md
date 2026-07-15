# CMake reference

ThreadSchedule requires CMake 3.14 and C++17. Linking the main target adds the
include directory, platform thread library, and a `cxx_std_17` requirement; it
does not force a newer standard selected by the consuming project.

## Targets

| Target | Availability | Purpose |
| --- | --- | --- |
| `ThreadSchedule::ThreadSchedule` | Always | Header-only C++17 API |
| `ThreadSchedule::Runtime` | `THREADSCHEDULE_RUNTIME=ON` | Optional shared process registry |

## Options

| Option | Default | Purpose |
| --- | :---: | --- |
| `THREADSCHEDULE_RUNTIME` | `OFF` | Build the optional C++ registry runtime |
| `THREADSCHEDULE_BUILD_TESTS` | `OFF` | Build the test suite |
| `THREADSCHEDULE_BUILD_EXAMPLES` | `OFF` | Build examples |
| `THREADSCHEDULE_BUILD_BENCHMARKS` | `OFF` | Build benchmarks |
| `THREADSCHEDULE_BUILD_DOCS` | `OFF` | Add the Doxygen target when available |
| `THREADSCHEDULE_INSTALL` | Top-level only | Generate install rules and package files |

## Header-only integration

```cmake
add_subdirectory(ThreadSchedule)
target_link_libraries(my_app PRIVATE ThreadSchedule::ThreadSchedule)
```

## Shared registry runtime

```cmake
set(THREADSCHEDULE_RUNTIME ON)
add_subdirectory(ThreadSchedule)

target_link_libraries(my_app PRIVATE ThreadSchedule::Runtime)
target_link_libraries(my_plugin PRIVATE ThreadSchedule::Runtime)
```

`ThreadSchedule::Runtime` propagates the header-only API, so consumers do not
need to link both targets. Every DSO must use the same ThreadSchedule headers,
compiler ABI, standard library ABI, architecture, and runtime-library mode.

## Package consumption

```cmake
find_package(ThreadSchedule 3 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE ThreadSchedule::ThreadSchedule)
```

The old `StableAbi` and `Module` targets and their associated options were
removed in 3.0. See [the migration guide](MIGRATION_V3.md).
