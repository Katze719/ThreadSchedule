## Unreleased

## v1.4.0

- Fix: `AutoRegisterCurrentThread` move constructor and move assignment now
  correctly transfer `externalReg_`, preventing unregister from the wrong
  registry after a move.
- Fix: Consistent MSVC C++20 detection (`_MSVC_LANG`) in `thread_wrapper.hpp`
  and `concepts.hpp`, matching the guard already used in
  `registered_threads.hpp`. Fixes compile errors on MSVC without
  `/Zc:__cplusplus`.
- Fix: `apply_profile` template can now be instantiated with `ThreadWrapper`,
  `JThreadWrapper`, `ThreadWrapperView`, `JThreadWrapperView`, and
  `PThreadWrapper` via new `is_thread_like` specialisations. Previously the
  template was constrained to `std::thread`/`std::jthread` which lack the
  required scheduling API.
- Added: `FastThreadPool::set_affinity()` and `FastThreadPool::wait_for_tasks()`
  for API parity with `ThreadPool` and `HighPerformancePool`.
- Added: Missing forwarding methods in `WithErrors` wrappers —
  `HighPerformancePoolWithErrors::set_affinity()`,
  `FastThreadPoolWithErrors::set_affinity()` and
  `FastThreadPoolWithErrors::wait_for_tasks()`.
- Improved: `JThreadWrapper` / `JThreadWrapperView` jthread-specific methods now
  use trailing return types, `[[nodiscard]]`, `const`, and `noexcept`
  consistently with the rest of the library.
- Improved: `ThreadPriority` factory methods are now `[[nodiscard]]` and
  `noexcept`; comparison operators are now `constexpr noexcept`.
- Improved: Added `[[nodiscard]]` to query methods across `WorkStealingDeque`,
  all pool classes, and `ScheduledTaskHandle`.
- Removed: Unused `thread_local std::random_device` in
  `HighPerformancePool::worker_function`.
- Added: C++20 coroutine primitive `task<T>` (`task.hpp`) — a lazy single-value
  coroutine that starts execution only when `co_await`ed. Includes full
  `task<void>` specialisation and exception propagation.
- Added: `sync_wait(task<T>)` / `sync_wait(task<void>)` — blocking bridge that
  runs a task on the calling thread and returns its result.
- Added: C++20 coroutine primitive `generator<T>` (`generator.hpp`) — a lazy
  multi-value coroutine producing elements via `co_yield`. Supports range-based
  for loops (`begin()` / `end()` with `std::default_sentinel_t`). Automatically
  aliases `std::generator<T>` when C++23 `__cpp_lib_generator` is available.
- Added: Coroutine exports in the C++20 module interface
  (`threadschedule.cppm`).

## v1.3.0

- Added: Build-mode introspection (`BuildMode` enum, `build_mode()`,
  `build_mode_string()`) to distinguish header-only from runtime builds at
  compile time and runtime.
- Added: C++20 module support (`src/threadschedule.cppm`) re-exporting the full
  public API.
- Added: C++26 standard support in CMake and Conan configuration.
- Updated: CI workflows for module compilation and extended standard coverage.
- Updated: README with module usage instructions and C++26 notes.

## v1.2.3

- Build/Style: Update `.clang-format` (`IndentPPDirectives: AfterHash`) for
  clearer preprocessor indentation.
- Core: Improve `expected.hpp` header detection — check `<version>` or
  `<experimental/version>` presence before including `<expected>`.
- Refactor: Simplify and clarify conditional compilation in `expected.hpp` for
  maintainability.

## v1.2.2

- fix: Debug builds of `ThreadScheduleRuntime` now output
  `libthreadscheduled.so` instead of `libthreadschedule.so` to distinguish debug
  from release artifacts

## v1.2.1

- fix build for some older mingw version
- fix ABI test

## v1.2.0

- Added: Windows thread affinity retrieval via `GetThreadGroupAffinity` in
  `include/threadschedule/thread_wrapper.hpp`
- Added: Integration test `integration_tests/runtime_abi_compat` to validate ABI
  compatibility (shared runtime) between current library and older tags
- Added: Parameterization for ABI test old version selection via
  `RUNTIME_ABI_OLD_REF` or `RUNTIME_ABI_OLD_OFFSET`
- Added: GitHub Actions workflow `abi-compat.yml` to run ABI tests on Linux and
  Windows for the last 3 tags; allowed failure only on major version bumps (or
  when explicitly enabled)
- Docs: Updated `integration_tests/README.md` with usage for ABI compatibility
  scenario

## v1.1.0

- Improve thread profile application (`apply_profile`)

## v1.0.0

- Refactor `ThreadControlBlock` and `RegisteredThreadInfo`

## v1.0.0-rc.5

- Add thread profiles, NUMA helpers, and chaos testing documentation
- Refactor `expected` class and error handling
- Update Doxygen to 1.14 and fix warnings

## v1.0.0-rc.4

- Add Doxygen documentation and theme integration
- Improve registry and control callbacks, ensure thread-safety
- Set name on control block creation

## v1.0.0-rc.3

- Documentation: registry diagrams and ownership clarifications
- Thread wrappers: ownership transfer methods and tests
- Benchmarks and documentation improvements

## v1.0.0-rc.2

- Chainable query API for thread registry
- Documentation and examples: scheduled tasks, error handling
- Roadmap and status in README
- Testing refactors and improvements

## v1.0.0-rc.1

- Integration testing framework and post-build steps
- App injection and composite merge libraries with registry support
- CI/documentation refinements

## v1.0.0-alpha.1

- Windows runtime post-build steps for integration tests
- Dynamic linking support on Windows for libraries
- Enforce shared runtime for MSVC
- CI improvements: ARM64, workflows, and documentation

## v0.4.0

- Global control registry and registry guide
- Non-owning thread views (`ThreadWrapperView`, `JThreadWrapperView`)
- CMake and CI modernization; expected type and tests

## v0.3.1

- CI refactor: split workflows, badges update, cleanup
- Fix CI meta

## v0.3.0

- Windows support for thread wrappers

## v0.2.0

- Benchmarks and resampling benchmark additions
- CMake refactor and integration guide

## v0.1.0

- Initial benchmark suite and examples
