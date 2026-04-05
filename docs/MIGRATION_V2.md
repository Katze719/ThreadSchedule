# Migrating to ThreadSchedule v2.0

This guide helps you move from v1.x to **v2.0.0**. It lists **breaking changes** first, then **behavioral changes** you should be aware of, and finally **optional upgrades** that are not required but often worthwhile.

For the authoritative list of every change, see [CHANGELOG.md](../CHANGELOG.md).

## 1. Upgrade steps

1. **Pin the version** in CMake / Conan / FetchContent to a v2.0.0 tag (or `main` once released).
2. **Rebuild** with the same `CMAKE_CXX_STANDARD` as before (v2 still supports C++17 as the baseline).
3. **Fix compile errors** using the sections below (most projects only touch `submit_range`, `configure_threads` storage type, or forward declarations).
4. **Run tests** -- especially anything that assumed strict per-element scheduling for `parallel_for_each` on `ThreadPool` / `FastThreadPool`.

## 2. Breaking changes (must fix)

### 2.1 `submit_range()` removed

`ThreadPool::submit_range` and `GlobalThreadPool::submit_range` are removed. Use **`submit_batch`** with the same iterators.

```cpp
// v1
auto futures = pool.submit_range(tasks.begin(), tasks.end());

// v2
auto futures = pool.submit_batch(tasks.begin(), tasks.end());
```

`submit_batch` acquires the queue lock once for the whole range and matches the API of `FastThreadPool` and `HighPerformancePool`.

### 2.2 `configure_threads` / `set_affinity` / `distribute_across_cpus` return type

On **`ThreadPool`** and **`FastThreadPool`**, these functions now return **`expected<void, std::error_code>`** (same as `HighPerformancePool` already did).

```cpp
// v1: storing in bool (no longer valid)
bool ok = pool.configure_threads("worker");

// v2: use auto or expected
auto r = pool.configure_threads("worker");
if (!r) {
    std::cerr << r.error().message() << '\n';
}

// Conditions still work: expected has operator bool
if (pool.configure_threads("worker")) { /* success */ }
```

### 2.3 `ThreadPool` and `FastThreadPool` are type aliases

They are now:

- `ThreadPool` = `ThreadPoolBase<IndefiniteWait>`
- `FastThreadPool` = `ThreadPoolBase<PollingWait<>>`

**Runtime behavior is unchanged.** You only need to act if you:

- **Forward-declared** a concrete `class ThreadPool;` -- forward-declare the alias or include the header instead.
- **Specialized** a template on `ThreadPool` as a unique class type -- switch to `ThreadPoolBase<IndefiniteWait>` (or a SFINAE-friendly trait).

### 2.4 `ThreadPool::Statistics` extended

`Statistics` on the single-queue pools now includes **`tasks_per_second`** and **`avg_task_time`**, like the other pools. If you use **designated initializers** or **memset**-style initialization that assumed a smaller struct, update the initializer list.

### 2.5 Error pool and global pool type names (aliases only)

These are now aliases; **the public API is unchanged**:

- `HighPerformancePoolWithErrors`, `ThreadPoolWithErrors`, `FastThreadPoolWithErrors` -> `PoolWithErrors<Pool>`
- `GlobalThreadPool`, `GlobalHighPerformancePool` -> `GlobalPool<Pool>`

Only unusual code (e.g. explicit template specialization on the old type name) may need the new spelling.

### 2.6 `ErrorHandler::add_callback` return type

`add_callback` now returns **`size_t`** (stable callback id for `remove_callback` / `has_callback`). Code that ignored the return value is unaffected. Code that assumed **`void`** must be updated.

```cpp
// v2
size_t id = handler.add_callback([](TaskError const& e) { /* ... */ });
handler.remove_callback(id);
```

## 3. API changes that are backward compatible

### 3.1 `shutdown()`

`shutdown()` now takes an optional **`ShutdownPolicy`** (default **`drain`**, matching old behavior). Old call sites without arguments behave as before.

```cpp
pool.shutdown();                                    // still: drain all work
pool.shutdown(ShutdownPolicy::drop_pending);      // new: drop queued tasks
pool.shutdown_for(std::chrono::seconds(5));         // new: timed drain
```

### 3.2 Destructors

Destructors still shut down the pool; they use **`drain`** by default. No change required unless you want **`drop_pending`** explicitly before destruction.

## 4. Behavioral changes (no rename, but semantics differ)

### 4.1 `parallel_for_each` on `ThreadPool` / `FastThreadPool`

Implementation is now **chunked** (same strategy as `HighPerformancePool`): the range is split into a small number of tasks instead of one task per element.

- **Pros:** Much less submission overhead on large ranges.
- **Cons:** Finer-grained progress / cancellation per element is no longer one-to-one with one pool task.

If you relied on **one future per element**, switch to an explicit loop with `submit`, or chunk manually.

### 4.2 Scheduled pools dispatch with `post()`

`ScheduledThreadPoolT` dispatches due tasks with **`post()`** instead of **`submit()`**, so **no `std::future` is created per dispatch**. Your task bodies are unchanged; only internal overhead is lower.

## 5. Optional improvements after migrating

These are **not** required for a successful build but match v2 design well:

| Goal | Approach |
| ---- | -------- |
| Less overhead than `submit()` | Use **`post()`** / **`try_post()`** when you do not need a return value or `std::future`. |
| Dedicated fire-and-forget pool | Use **`LightweightPool`** / **`LightweightPoolT<N>`** (SBO task buffer, no futures). |
| Non-throwing submit | Use **`try_submit()`** / **`try_submit_batch()`** and check **`expected`**. |
| Tune fast pool polling | Use **`ThreadPoolBase<PollingWait<Ms>>`** or keep **`FastThreadPool`** (10 ms default). |
| Tune HP deque size | **`HighPerformancePool(threads, deque_capacity)`**. |
| Fix global pool size early | **`GlobalPool<...>::init(n)`** before first **`instance()`**. |
| Workers in registry | Pass **`register_workers = true`** to pool constructors. |

## 6. Header and module notes

- New headers pulled in by the umbrella header include **`futures.hpp`** (combinators) and coroutine helpers on **`task.hpp`** as documented in [COROUTINES.md](COROUTINES.md).
- Include **`threadschedule/futures.hpp`** directly if you only need combinators.

## 7. Further reading

- [README.md](../README.md) -- "What's new in v2.0" summary table
- [CHANGELOG.md](../CHANGELOG.md) -- full v2.0.0 notes
- [INTEGRATION.md](INTEGRATION.md) -- CMake and package managers
- [ERROR_HANDLING.md](ERROR_HANDLING.md) -- pools with errors and callbacks
- [SCHEDULED_TASKS.md](SCHEDULED_TASKS.md) -- scheduled pools and aliases
