# AGENTS.md

This file applies to the entire repository.

## Project direction

ThreadSchedule 3.x is a C++17-first library with a small, lowercase public API.
Keep the public surface familiar to users of the C++ standard library while
returning `threadschedule::expected<T, std::error_code>` from operations whose
normal failure mode should not require exceptions.

- Public types and functions use lowercase `snake_case` names.
- The v3 core API owns its implementation. Do not rebuild removed PascalCase
  APIs as aliases, forwarding wrappers, compatibility headers, or `using`
  declarations.
- `threadschedule::thread` should behave like `std::thread`: direct
  construction is the normal path. A `create(...)` factory may remain as an
  optional error-returning path, but callers must not be forced to use it.
- Use `std::thread` as the owning thread implementation. Do not reintroduce
  `PThreadWrapper`, `ThreadWrapper`, `BasicThreadWrapper`, or equivalent public
  wrapper families.
- When C++20 and `std::jthread` are available, expose the independent
  `threadschedule::jthread` API with standard-style callable forwarding and
  stop-token injection. Do not provide a C++17 emulation of `jthread`.
- Platform-specific pthread and Windows code belongs behind native-control
  implementation boundaries, not in a second public owning-thread type.
- Internal backend types may live in `threadschedule::detail`, but examples,
  documentation, and integration tests must use the public v3 API.
- The default build is header-only. Preserve the optional shared registry
  runtime and its `ThreadSchedule::Runtime` CMake target.

## Compatibility

- C++17 is the baseline and must remain fully functional.
- C++20 adds `jthread`; C++23 and C++26 must not silently change the layout or
  behavior of library-owned public/internal storage types.
- Supported environments are GCC/libstdc++ on Linux, MinGW-w64/GCC, and MSVC.
  Avoid relying on libc++-only behavior or newer standard-library facilities
  without a guarded fallback.
- Never expose platform handles or toolchain-specific ABI details through the
  portable core unless the API is explicitly under `threadschedule::advanced`.
- The optional runtime is a same-toolchain C++ ABI, not a portable plugin ABI.

## Repository layout

- `include/threadschedule/`: public headers and header-only implementation
- `include/threadschedule/detail/`: non-public implementation details
- `src/`: optional shared runtime sources
- `tests/`: unit and API tests
- `examples/`: compile-tested public API examples
- `integration_tests/`: installed-package and multi-DSO registry scenarios
- `docs/`: user-facing API, compatibility, CMake, and migration documentation
- `.github/workflows/`: authoritative CI matrix

## Build and test

Use an out-of-source build. Disable ccache when validating compiler-sensitive
changes so stale cache entries cannot hide failures.

```sh
cmake -S . -B build-local -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_STANDARD=17 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DTHREADSCHEDULE_BUILD_TESTS=ON \
  -DTHREADSCHEDULE_BUILD_EXAMPLES=ON
CCACHE_DISABLE=1 cmake --build build-local --parallel
ctest --test-dir build-local --output-on-failure --parallel 2
```

For changes touching `jthread`, stop tokens, feature detection, or generic
callable storage, repeat with a separate C++20 build. Changes to callable
storage, templates, or compatibility guards should also be compiled under
C++23 and C++26. Changes to native controls, exported APIs, CMake packaging, or
the runtime require the relevant MinGW/MSVC and integration coverage from the
CI matrix.

To validate the optional runtime locally, add:

```sh
-DTHREADSCHEDULE_RUNTIME=ON
```

Installed-package behavior must be tested from a fresh install prefix before
running `integration_tests/app_injection`, `composite_merge`, or
`runtime_single`. Do not let those scenarios accidentally consume headers or
targets from the source tree.

## Formatting and static analysis

The checked-in `.clang-format` and `.clang-tidy` files are authoritative. They
intentionally follow the public style of GCC's libstdc++ without copying its
reserved identifiers.

- Run `clang-format` on every changed C++ source/header and verify with
  `clang-format --dry-run --Werror`.
- Keep lines within the configured 79-column limit.
- Use lowercase identifiers and a trailing underscore for private/protected
  data members.
- Run clang-tidy with warnings as errors against a compile database:

```sh
run-clang-tidy -p build-local -j 4 -quiet -warnings-as-errors='*' \
  '/absolute/path/to/ThreadSchedule/(src|tests|examples)/.*\.cpp$'
```

- For public header or template changes, run clang-tidy in both C++17 and
  C++20 configurations. Also run it on changed integration sources using their
  own compile databases.
- A focused `clangd --check=<file> --compile-commands-dir=<build-dir>` is useful
  for changed examples and public API consumers.

## Tests and API changes

- Add or update tests for every behavior change and regression fix.
- Prefer public API tests for public behavior. Backend tests are appropriate
  only for implementation-specific contracts.
- Test short-lived threads when changing startup/configuration code; native
  configuration must not race with a thread that exits immediately.
- Test move-only callables under C++17 as well as newer standards.
- Tests involving realtime scheduling, affinity, or elevated privileges must
  handle documented permission failures rather than assuming root access.
- Keep integration tests concise, deterministic, and based only on installed
  public headers and exported CMake targets.

## Documentation and release hygiene

- Update `README.md` when the recommended first-use path changes.
- Update `docs/API.md`, `docs/ADVANCED.md`, or `docs/CMAKE_REFERENCE.md` when
  their documented contracts change.
- Update `docs/MIGRATION_V3.md` for any breaking 2.x-to-3.x migration detail.
- Record user-visible changes in `CHANGELOG.md` under the current release.
- Keep examples buildable with C++17 unless they are explicitly guarded C++20
  `jthread` examples.
- Historical names may appear in the changelog and migration guide. Do not use
  them in current API examples or integration code.

## Before handing off

At minimum:

1. Build and run the relevant C++17 tests.
2. Run C++20 tests when feature-gated code is affected.
3. Run clang-format, clang-tidy, and `git diff --check`.
4. Build the examples and any affected installed-package integrations.
5. Inspect the final diff and preserve unrelated user changes.
6. Report exactly which configurations were run and any platform matrix that
   remains CI-only.
