# Migrating to ThreadSchedule v3.0

This guide tracks the breaking changes for ThreadSchedule 3.0.0. It is kept on
the 3.0.0 preparation branch and should be updated as implementation lands.

For the authoritative release list, see [CHANGELOG.md](../CHANGELOG.md).
For the target API shape, see [API_DESIGN_V3.md](API_DESIGN_V3.md).

## 1. Upgrade Strategy

1. Move to the 3.0.0 release branch or tag.
2. Rebuild with C++17 or newer.
3. If your code crosses shared-library or plugin boundaries, migrate exported
   ThreadSchedule usage to `threadschedule::abi::*`.
4. Replace legacy 2.x convenience entry points with the unified v3 wrappers.
5. Update priority and scheduling code to the new intent-based API.
6. Run unit, integration, and mixed-standard ABI tests.

## 2. Major Breaking Themes

### 2.1 Stable ABI Is The Runtime Boundary

The supported cross-DSO boundary is the stable ABI layer only. Exported runtime
functions must use opaque handles, POD structs, fixed-width enums, callbacks,
and status codes.

Code that exports or consumes these C++ types across binary boundaries must be
changed:

- `ThreadRegistry`
- `RegisteredThreadInfo`
- `AutoRegisterCurrentThread`
- pool classes
- thread wrapper classes
- STL-heavy callback signatures
- `std::future` or `expected` in exported signatures

Use ABI handles and C++ wrapper objects over those handles instead.

### 2.2 Unified Public C++ API

The v3 C++ API should have one consistent style for registry, thread, pool,
scheduled task, and profile operations. Duplicated aliases and compatibility
names from 2.x may be removed.

Expected migration pattern:

```cpp
// v2 style: several class families and direct runtime C++ objects
threadschedule::ThreadPool pool(4);
auto future = pool.submit([] { return 42; });

// v3 target style: one public wrapper style over stable runtime handles
auto pool = threadschedule::thread_pool::create({.threads = 4});
auto future = pool.submit([] { return 42; });
```

The exact wrapper spellings will be finalized in `API_DESIGN_V3.md` before the
implementation is completed.

### 2.3 Priority And Scheduling Are Redesigned

The priority and scheduling API is rebuilt around user intent.

Common operations should read like:

```cpp
thread.configure(threadschedule::schedule::background());
thread.configure(threadschedule::schedule::low_latency());
thread.configure(threadschedule::schedule::realtime_fifo(80));
thread.configure(threadschedule::schedule::normal());
```

Advanced native control remains available, but it should be visibly advanced
and policy-specific. Code that used generic `ThreadPriority::highest()` or raw
integer priorities may need to migrate to explicit scheduling requests.

### 2.4 Pools Move Behind ABI Handles

Pool operations that must cross binary boundaries use `pool_handle` and
callbacks. Futures remain a C++ wrapper feature and are not part of the exported
ABI.

## 3. Compatibility Expectations

- C++17 remains supported.
- C++20/C++23/C++26 helpers remain source-level conveniences.
- A C++17 library should be able to expose ThreadSchedule ABI handles that a
  C++23 consumer can use safely.
- Header-only mode remains source-level only and is not a stable ABI promise.

## 4. Migration Checklist

- Replace exported C++ ThreadSchedule types with ABI handles.
- Replace direct exported callbacks using STL types with function pointer plus
  `void* user_data`.
- Replace direct pool exports with pool handles or app-owned wrapper APIs.
- Replace raw priority integers with intent-based scheduling requests.
- Audit exception behavior at boundaries; convert ABI-facing failures to status
  codes.
- Add mixed-standard integration tests for every exported wrapper library.
