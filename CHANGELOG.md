# Changelog

## v2.3.0

> **No intended API/ABI breaking changes for existing non-reflection users.**
> This release adds an optional GCC-16/C++26 reflection surface and uses it to
> expose faster registry projection/filter paths without changing the existing
> query API.

### New Features

- **Optional GCC 16.1+ reflection API** -- when building with C++26,
  `THREADSCHEDULE_ENABLE_REFLECTION=ON`, and working `-freflection` support,
  the library now exports `threadschedule::reflect::*` for field metadata,
  field visitation, compile-time projection, and type/field naming.
  (`reflection.hpp`, `threadschedule.cppm`, `CMakeLists.txt`)

- **Reflection-backed registry selectors** -- `ThreadRegistry` and
  `QueryView` now expose field-oriented helpers such as
  `where<registered_thread_fields::componentTag()>(...)`,
  `where_if<registered_thread_fields::alive()>(...)`,
  `find_by<registered_thread_fields::name()>(...)`,
  `contains<...>(...)`, and `project<...>()` when reflection is enabled.
  (`thread_registry.hpp`)

### Performance

- **Lower-overhead registry projections on reflection builds** -- direct
  field-projection and field-filter paths now run under the registry's shared
  lock and can skip the older `filter(...).map(...)` layering when callers opt
  into the new reflection APIs. This reduces intermediate traversal and avoids
  some full-entry transformation work for hot query paths. (`thread_registry.hpp`)

- **More metadata is now promoted at compile time** -- reflection field names
  and type display names are now stabilized via `std::define_static_string(...)`
  and reused through `consteval` helpers such as `field_names<T>()`, reducing
  repeated compile-time reconstruction of the same metadata. (`reflection.hpp`)

### Documentation

- **README examples for reflection queries** -- the top-level README now shows
  how to combine `threadschedule::reflect` with field-based registry queries
  and projections. (`README.md`)

- **New CMake reference entry for reflection** -- the reference now documents
  `THREADSCHEDULE_ENABLE_REFLECTION` and the GCC 16.1+/C++26 activation path.
  (`docs/CMAKE_REFERENCE.md`)

### Tests & Benchmarks

- **New reflection unit coverage** -- dedicated tests now validate reflection
  metadata for core public structs and reflection-backed registry queries.
  (`tests/reflection_test.cpp`, `tests/registry_query_test.cpp`,
  `tests/CMakeLists.txt`)

- **New reflection registry benchmark** -- `reflection_registry_benchmarks`
  compares classic `filter/map/find_if` usage against the new field-oriented
  query helpers on synthetic registry snapshots. (`benchmarks/CMakeLists.txt`,
  `benchmarks/reflection_registry_benchmarks.cpp`)

### CI / Infrastructure

- **Dedicated GCC 16 reflection CI jobs** -- the main test workflow now
  includes explicit `ubuntu-24.04` jobs for reflection-enabled GCC 16/C++26
  validation: one job builds and runs the reflection-focused test cases, and a
  second job verifies the reflection-enabled module build path. This makes the
  new `THREADSCHEDULE_ENABLE_REFLECTION` surface visible in CI instead of
  relying only on the generic C++26 matrix entry. (`.github/workflows/tests.yml`)

## v2.2.0

> **No intended API/ABI breaking changes.** This release extends thread-control
> coverage to library-owned background threads and expands `ThreadInfo` into a
> lightweight per-thread control handle.

### New Features

- **`ThreadInfo` now supports bound thread IDs** -- it can be default-constructed
  for the current thread or explicitly constructed from a `Tid`, then used to
  `set_name`, `get_name`, `set_priority`, `set_scheduling_policy`,
  `set_affinity`, `get_affinity`, `get_policy`, and `get_priority`.
  The existing static convenience methods remain available. (`thread_wrapper.hpp`)

- **Library-owned background threads are now configurable** -- `ScheduledThreadPoolT`
  exposes `scheduler_thread_info()` and `configure_scheduler_thread(...)`, and
  `ChaosController` exposes `thread_info()` and `configure_thread(...)`, so the
  scheduler/control threads are no longer anonymous internal `std::thread`s.
  (`scheduled_pool.hpp`, `chaos.hpp`)

### Internal Improvements

- **Dedicated background threads now use the same wrapper/control path as worker
  threads** -- scheduler and chaos threads are created as `ThreadWrapper`s and
  receive stable default names, keeping thread-control behavior consistent
  across the library. (`scheduled_pool.hpp`, `chaos.hpp`)

- **Callable storage is now feature-gated by language/library support** --
  internal task and callback paths use modern standard call wrappers when they
  are available: move-only task queues can use `std::move_only_function`
  (C++23+ libraries), reusable hooks/callbacks can use
  `std::copyable_function` (C++26-capable libraries), and older standards keep
  the `std::function` fallback. Public aliases remain source-compatible while
  new templated setter/registration overloads avoid unnecessary type-erasure
  constraints. (`callable.hpp`, `thread_pool.hpp`, `scheduled_pool.hpp`,
  `error_handler.hpp`, `thread_registry.hpp`, `thread_pool_with_errors.hpp`,
  `pthread_wrapper.hpp`)

### Performance

- **Move-only tasks are now supported on more hot paths** -- `post`/`try_post`
  and scheduler one-shot dispatch can carry move-only captures directly instead
  of forcing a copyable `std::function` path on newer standard libraries.
  This reduces adaptation overhead for fire-and-forget workloads and enables
  more modern task payloads without wrapper glue. (`thread_pool.hpp`,
  `scheduled_pool.hpp`, `thread_pool_with_errors.hpp`, `pthread_wrapper.hpp`)

### Tests

- **New regression coverage for modern callable paths** -- tests now cover
  move-only `post` tasks, move-only scheduled tasks, move-only
  `FutureWithErrorHandler::on_error(...)` callbacks, `PoolWithErrors` with
  move-only arguments, and `ThreadInfo(Tid)` invalid-target behavior.
  (`thread_pool_v2_test.cpp`, `futures_test.cpp`, `thread_config_test.cpp`)

- **New callable benchmark target** -- `callable_benchmarks` compares small
  capture, large capture, and move-only capture posting overhead on
  `ThreadPool` and `HighPerformancePool` as a local performance validation
  tool. (`benchmarks/callable_benchmarks.cpp`, `benchmarks/CMakeLists.txt`)

### CI / Infrastructure

- **Added Linux C++26 coverage for GCC 16 and Clang 22** -- the main test
  workflow now installs and runs additional `ubuntu-24.04` jobs for
  `gcc-16`/`g++-16` and `clang-22`/`clang++-22`, extending verification of the
  modern callable and C++26 code paths without replacing the existing matrix.
  (`.github/workflows/tests.yml`)

## v2.1.0

> **No API/ABI breaking changes.** All modifications are bug fixes (aligning
> behaviour with documented API), internal optimizations, additive overloads,
> new classes, and new tests/infrastructure.

### Bug Fixes

- **`when_all<T>` no longer requires default-constructible `T`** -- the
  `results.emplace_back()` on the exception path was removed. The vector is
  never consumed when an exception is rethrown. (futures.hpp)

- **`when_any` no longer busy-polls at 1 ms** -- exponential backoff
  (1 ms → 16 ms cap) and a randomized start index eliminate CPU waste and
  index bias. Empty input now throws `std::invalid_argument` instead of
  looping forever. (futures.hpp)

- **`ScheduledThreadPoolT::insert_task` checks `stop_`** -- scheduling a
  task after `shutdown()` now returns a pre-cancelled `ScheduledTaskHandle`
  instead of silently inserting a task that will never execute.
  (scheduled_pool.hpp)

- **`ChaosController` uses actual thread priority** -- priority jitter now
  reads the real scheduling priority via `sched_getparam()` on Linux instead
  of hardcoding `ThreadPriority::normal()`. (chaos.hpp)

- **`ErrorHandler::handle_error` releases the lock before invoking
  callbacks** -- callbacks are snapshot-copied under the mutex, then executed
  outside the critical section, eliminating deadlock risk when callbacks
  interact with the handler. (error_handler.hpp)

- **`PoolWithErrors` documentation corrected** -- the doc comment now says
  "implicitly movable" instead of the incorrect "non-movable".
  (thread_pool_with_errors.hpp)

### Performance

- **`distribute_affinities_by_numa` calls `read_topology()` once** -- the
  previous implementation read sysfs O(n) times for n threads. New additive
  overloads `affinity_for_node(CpuTopology const&, ...)` and
  `distribute_affinities_by_numa(CpuTopology const&, ...)` accept a
  pre-read topology snapshot. (topology.hpp)

### New Features

- **`InlinePool`** -- deterministic, single-threaded pool that executes every
  task synchronously on the calling thread. Same `submit`/`post`/`try_submit`
  API as `ThreadPool`, making it a drop-in for unit tests.
  (inline_pool.hpp)

- **`task_group<Pool>`** -- structured concurrency primitive. All submitted
  tasks are guaranteed to complete before `wait()` returns (or the destructor
  runs). First exception is captured and rethrown from `wait()`.
  (task_group.hpp)

- **`PoolWithErrors` forwarding constructor** -- new 2+ argument constructor
  forwards pool-specific arguments (e.g. `deque_capacity` for
  `HighPerformancePool`). (thread_pool_with_errors.hpp)

- **`apply_profile_detailed()`** -- new function returning a
  `std::vector<std::error_code>` with one entry per configuration step,
  unlike `apply_profile()` which aggregates into a single error code.
  (profiles.hpp)

### Module Exports

- Added missing exports to `threadschedule.cppm`: `when_all`, `when_any`,
  `when_all_settled`, `ShutdownPolicy`, `IndefiniteWait`, `PollingWait`,
  `ThreadPoolBase`, `LightweightPoolT`, `LightweightPool`, `GlobalPool`,
  `PoolWithErrors`, `ScheduledLightweightPool`, `TaskStartCallback`,
  `TaskEndCallback`, `schedule_on`, `run_on`, `pool_executor`, `InlinePool`,
  `task_group`, `apply_profile_detailed`.

### Tests

- **65 new Google Test cases** across four new test files:
  - `thread_pool_v2_test.cpp` -- `try_submit`, `try_post`, `submit_batch`,
    `parallel_for_each`, `ShutdownPolicy`, `LightweightPool`, `GlobalPool`,
    `ScheduledThreadPool`, stop-token tasks, `InlinePool`, `task_group`.
  - `futures_test.cpp` -- `when_all`, `when_any`, `when_all_settled` (typed
    and void variants, empty input, exception propagation).
  - `registry_query_test.cpp` -- chainable `QueryView` API: `filter`, `map`,
    `for_each`, `find_if`, `any`/`all`/`none`, `take`, `skip`.
  - `coroutine_pool_test.cpp` -- `schedule_on`, `run_on`, `pool_executor`,
    nested awaits, cross-pool hops, exception propagation (C++20 coroutines).

### CI / Infrastructure

- **New `sanitizers.yml` workflow** with:
  - **ASan** (AddressSanitizer + LeakSanitizer)
  - **TSan** (ThreadSanitizer)
  - **UBSan** (UndefinedBehaviorSanitizer)
  - **Code coverage** job (gcov + lcov, artifact upload)
  - **Clang-Tidy** job (Clang 19, C++20)

---

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
