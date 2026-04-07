# Changelog

## v2.0.0

### Breaking Changes

- **`ThreadPool` and `FastThreadPool` are now type aliases** for
  `ThreadPoolBase<IndefiniteWait>` and `ThreadPoolBase<PollingWait>`. Behavior
  is unchanged, but code that forward-declares or specializes on the concrete
  class name may need adjustment.

- **`configure_threads()`, `set_affinity()`, `distribute_across_cpus()`** on
  `ThreadPool` and `FastThreadPool` now return `expected<void, std::error_code>`
  instead of `bool`. `HighPerformancePool` already used this return type.
  Migration: `if (pool.configure_threads(...))` still compiles (expected has
  `operator bool`), but code that stores the result in a `bool` variable needs
  updating to `auto` or the expected type.

- **`ThreadPool::Statistics`** now includes `tasks_per_second` and
  `avg_task_time` fields (previously only on `FastThreadPool` and
  `HighPerformancePool`).

- **`submit_range()` removed** from `ThreadPool`. Use `submit_batch()` instead
  (consistent with `FastThreadPool` and `HighPerformancePool`). `submit_batch()`
  is also more efficient: it acquires the queue lock once for the entire batch
  instead of per-item.

- **`GlobalThreadPool::submit_range()` removed**. Use
  `GlobalThreadPool::submit_batch()`.

- **`HighPerformancePoolWithErrors`, `FastThreadPoolWithErrors`,
  `ThreadPoolWithErrors`** are now type aliases for `PoolWithErrors<Pool>`. The
  public API is unchanged.

- **`GlobalThreadPool`, `GlobalHighPerformancePool`** are now type aliases for
  `GlobalPool<Pool>`. The public API is unchanged.

### Quality-of-Life Features

- **`ErrorHandler::remove_callback(id)` / `has_callback(id)`** -- callbacks are
  now stored in a `std::map` with stable IDs. Individual callbacks can be
  removed without clearing all of them.

- **`try_submit()` / `try_submit_batch()`** -- non-throwing submission for all
  pool types, returning `expected<std::future<T>, std::error_code>` instead of
  throwing on shutdown.

- **Chunked `parallel_for_each`** -- `ThreadPoolBase` now uses the same chunked
  work distribution as `HighPerformancePool` via a shared
  `detail::parallel_for_each_chunked` helper (one task per element is gone).

- **`PollingWait<IntervalMs>`** -- tunable polling interval (default 10 ms).
  `FastThreadPool` is `ThreadPoolBase<PollingWait<>>`.

- **`HighPerformancePool` deque capacity** -- configurable via constructor:
  `HighPerformancePool(threads, deque_capacity)`.

- **`GlobalPool::init(n)`** -- pre-configure thread count before first use
  (std::call_once semantics).

- **C++20 ranges overloads** -- `submit_batch(range)`,
  `try_submit_batch(range)`, `parallel_for_each(range, func)` on all pool types
  and GlobalPool. Guarded by `__cpp_lib_ranges`.

- **Auto-register pool workers** -- opt-in `register_workers` flag on both pool
  constructors. Workers register/unregister automatically via
  `AutoRegisterCurrentThread` RAII guard.

- **Per-task tracing hooks** -- `set_on_task_start(callback)` and
  `set_on_task_end(callback)` on both pool types. Callbacks receive timestamp,
  thread ID, and (for end) elapsed duration.

- **Cooperative cancellation** -- `submit(stop_token, F, Args...)` and
  `try_submit(stop_token, F, Args...)` overloads. Tasks are skipped if stop is
  requested. Guarded by `__cpp_lib_jthread`.

- **Future combinators** -- new `futures.hpp` with `when_all`, `when_any`,
  `when_all_settled` (typed and void specializations).

- **Lifecycle modes** -- `ShutdownPolicy::drain` (default) and
  `ShutdownPolicy::drop_pending`. `shutdown(policy)` replaces the old
  no-argument `shutdown()`. `shutdown_for(timeout)` provides timed drain.

- **Coroutine scheduler integration** -- `schedule_on{pool}` awaitable to hop to
  a pool thread, `executor_base` / `pool_executor<Pool>` type-erased executor
  for pool-aware tasks, `run_on(pool, coro_fn)` convenience returning
  `std::future`.

- **`LightweightPoolT<TaskSize>`** -- ultra-lightweight fire-and-forget pool
  using a custom `detail::SboCallable<TaskSize>` with configurable inline buffer
  (default 64 bytes = 1 cache line, 56 bytes usable). Zero heap allocations for
  typical lambdas. No futures, no `packaged_task`, no statistics, no tracing.
  Workers are `ThreadWrapper` so `configure_threads`/`set_affinity` still work.
  `using LightweightPool = LightweightPoolT<>` for the default.

- **`post()` / `try_post()`** -- fire-and-forget submission on all pool types
  (`HighPerformancePool`, `ThreadPoolBase`, `GlobalPool`). Same queue logic as
  `submit()` but skips `packaged_task`/`shared_ptr`/`future` overhead.

- **`ScheduledThreadPoolT` now uses `post()`** internally instead of `submit()`,
  eliminating wasted `future` allocations for every scheduled task dispatch. New
  alias: `ScheduledLightweightPool = ScheduledThreadPoolT<LightweightPool>`.

### New Types

- `ThreadPoolBase<WaitPolicy>` - parameterized single-queue thread pool.
- `IndefiniteWait` / `PollingWait<IntervalMs>` - wait policy types for
  `ThreadPoolBase`.
- `PoolWithErrors<PoolType>` - generic error-handling pool wrapper.
- `GlobalPool<PoolType>` - generic singleton pool accessor.
- `ShutdownPolicy` - enum controlling shutdown behavior (drain / drop_pending).
- `TaskStartCallback` / `TaskEndCallback` - tracing callback types.
- `executor_base` / `pool_executor<Pool>` - type-erased executor for coroutines.
- `schedule_on<Pool>` - awaitable for hopping to a pool thread.
- `futures.hpp` - future combinators (`when_all`, `when_any`,
  `when_all_settled`).
- `LightweightPoolT<TaskSize>` / `LightweightPool` - fire-and-forget pool with
  SBO.
- `detail::SboCallable<TaskSize>` - type-erased callable with inline storage.
- `ScheduledLightweightPool` - scheduled pool backed by `LightweightPool`.

### Internal Improvements

- **~1000 lines of code duplication removed** across `thread_pool.hpp`,
  `thread_pool_with_errors.hpp`, `thread_wrapper.hpp`, `thread_registry.hpp`,
  `pthread_wrapper.hpp`, `profiles.hpp`, and `scheduled_pool.hpp`.

- **Priority / affinity / scheduling policy** OS-level logic centralized into
  `detail::apply_priority()`, `detail::apply_scheduling_policy()`, and
  `detail::apply_affinity()` free functions (overloaded for `pthread_t`,
  `pid_t`, and `HANDLE`). `BaseThreadWrapper`, `ThreadControlBlock`,
  `PThreadWrapper`, and `ThreadByNameView` now delegate to these shared
  implementations.

- **`apply_profile()` overloads** refactored to use shared
  `detail::apply_profile_to()` and `detail::apply_profile_to_pool()` helpers.

- **`ScheduledThreadPoolT`**: `schedule_at()` and `schedule_periodic_after()`
  now share a private `insert_task()` helper.

- **Pool worker configuration deduplicated**: `configure_threads()`,
  `set_affinity()`, `distribute_across_cpus()` in `HighPerformancePool` and
  `ThreadPoolBase` now delegate to shared `detail::configure_worker_threads`,
  `detail::set_worker_affinity`, `detail::distribute_workers_across_cpus`
  templates.

- **Thread naming/affinity reading centralized**: `set_name()`, `get_name()`,
  `get_affinity()` across `BaseThreadWrapper`, `PThreadWrapper`, and
  `ThreadControlBlock` now delegate to `detail::apply_name`,
  `detail::read_name`, `detail::read_affinity` in `scheduler_policy.hpp`.

- **`FutureWithErrorHandler<void>` specialization removed**: The primary
  template now handles both `T` and `void` via `if constexpr`, eliminating ~70
  lines of duplicated code. No API change.

- **`CompositeThreadRegistry` facade deduplicated**: The 12 query facade methods
  (filter, map, for_each, find_if, any, all, none, take, skip, count, empty,
  apply) are now inherited from `detail::QueryFacadeMixin<Derived>` CRTP base.
  No API change.

- **`ThreadRegistry` inherits `detail::QueryFacadeMixin`**: The 12 facade
  methods (filter, map, for_each, find_if, any, all, none, take, skip, count,
  empty, apply) are now provided by the same CRTP mixin as
  `CompositeThreadRegistry`, eliminating the duplicate implementations.

- **POSIX scheduling helpers consolidated**: `apply_priority` and
  `apply_scheduling_policy` for both `pthread_t` and `pid_t` now share a common
  `detail::apply_sched_params` template, eliminating duplicated param validation
  and error handling.

- **`ThreadRegistry::register_current_thread` consolidated**: Both overloads now
  delegate to a private `try_register(RegisteredThreadInfo)` method, removing
  the duplicated lock/emplace/callback logic.

- **`PoolWithErrors` submit methods consolidated**: `submit()` and
  `submit_with_description()` now delegate to a private `submit_impl` with
  optional description parameter.

- **`TaskError::capture()` factory**: New static factory method centralizes the
  repeated exception/thread_id/timestamp capture pattern. Used by
  `ErrorHandledTask` and `PoolWithErrors`.

- **`ThreadControlBlock` native handle accessor**: Private `native_handle()`
  method replaces four identical `#ifdef _WIN32` dispatch blocks in the
  set_affinity/set_priority/set_scheduling_policy/set_name methods.

### Migration Guide

Full step-by-step guide: **[docs/MIGRATION_V2.md](docs/MIGRATION_V2.md)**.

Quick reference:

```cpp
// v1: bool return
bool ok = pool.configure_threads("worker");

// v2: expected return (operator bool still works in conditions)
auto result = pool.configure_threads("worker");
if (!result.has_value()) {
    std::cerr << result.error().message() << std::endl;
}

// v1: submit_range
auto futures = pool.submit_range(tasks.begin(), tasks.end());

// v2: submit_batch (same signature, more efficient)
auto futures = pool.submit_batch(tasks.begin(), tasks.end());
```

## v1.4.1

- Fix: `*WrapperReg` types (`ThreadWrapperReg`, `JThreadWrapperReg`,
  `PThreadWrapperReg`) now have explicit move constructor and move assignment
  operator, enabling default-construct-then-assign patterns (e.g.
  `JThreadWrapperReg t; t = JThreadWrapperReg(...);`).
- Fix: `*WrapperReg` wrapping lambdas now use `std::invoke`, so member function
  pointers work as callables (e.g.
  `JThreadWrapperReg("n", "c", &MyClass::run, this)`).
- Fix: `JThreadWrapperReg` now correctly forwards `std::stop_token` to callables
  that accept it, while also supporting callables without `stop_token` - the
  previous `auto&&...` wrapper always claimed to accept a token, causing a
  compile error when the user's callable did not.

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
- Added: Missing forwarding methods in `WithErrors` wrappers -
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
- Added: C++20 coroutine primitive `task<T>` (`task.hpp`) - a lazy single-value
  coroutine that starts execution only when `co_await`ed. Includes full
  `task<void>` specialisation and exception propagation.
- Added: `sync_wait(task<T>)` / `sync_wait(task<void>)` - blocking bridge that
  runs a task on the calling thread and returns its result.
- Added: C++20 coroutine primitive `generator<T>` (`generator.hpp`) - a lazy
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
- Core: Improve `expected.hpp` header detection - check `<version>` or
  `<experimental/version>` presence before including `<expected>`.
- Refactor: Simplify and clarify conditional compilation in `expected.hpp` for
  maintainability.

## v1.2.2

- fix: Debug builds of `ThreadScheduleRuntime` now output
  `libthreadscheduled.so` instead of `libthreadschedule.so` to distinguish debug
  from release artifacts

- Build: Conditionally enable `-Wnrvo` via compiler feature detection
- Fix: Ensure NRVO in `affinity_for_node` by returning a single named local
  `ThreadAffinity` on all paths (removes `-Wnrvo` warning).

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
