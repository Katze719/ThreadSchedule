# Coroutine Primitives

ThreadSchedule provides lightweight C++20 coroutine building blocks that let you
write asynchronous-looking code without building your own promise types.

## Requirements

- C++20 compiler with coroutine support (`<coroutine>` header)
- Automatically detected via `__cpp_impl_coroutine >= 201902L`
- Headers are no-ops on C++17 builds (the guards simply exclude all content)

## Features

- **`task<T>`** -- Lazy single-value coroutine that starts only when `co_await`ed
- **`task<void>`** -- Void specialisation for side-effect-only coroutines
- **`sync_wait(task<T>)`** -- Blocking bridge to run a task from synchronous code
- **`generator<T>`** -- Lazy sequence coroutine producing values via `co_yield`
- Automatic `std::generator<T>` alias when C++23 `__cpp_lib_generator` is
  available

## Quick Start

```cpp
#include <threadschedule/threadschedule.hpp>
// or include individually:
// #include <threadschedule/task.hpp>
// #include <threadschedule/generator.hpp>

using namespace threadschedule;

task<int> compute(int x) {
    co_return x * 2;
}

int main() {
    int result = sync_wait(compute(21)); // 42
}
```

## `task<T>` -- Lazy Single-Value Coroutine

A `task<T>` represents a computation that will produce exactly one value (or
throw). It is **lazy**: the coroutine body does not execute until someone
`co_await`s the task or passes it to `sync_wait`.

### Basic usage

```cpp
task<int> add(int a, int b) {
    co_return a + b;
}

task<std::string> greet(std::string name) {
    co_return "Hello, " + name + "!";
}
```

### Composing tasks (nested co_await)

Tasks can await other tasks, forming a call chain that executes lazily:

```cpp
task<int> fetch_value() {
    co_return 10;
}

task<int> double_it() {
    int v = co_await fetch_value();   // starts fetch_value here
    co_return v * 2;
}

task<int> pipeline() {
    int a = co_await double_it();     // 20
    int b = co_await fetch_value();   // 10
    co_return a + b;                  // 30
}
```

### `task<void>`

For coroutines that perform work but return no value:

```cpp
task<void> log_message(std::string msg) {
    std::cout << msg << "\n";
    co_return;
}

task<void> run() {
    co_await log_message("step 1");
    co_await log_message("step 2");
}
```

### Exception propagation

Exceptions thrown inside a task are captured and re-thrown when the result is
retrieved (via `co_await` or `sync_wait`):

```cpp
task<int> might_fail() {
    throw std::runtime_error("oops");
    co_return 0;
}

task<void> caller() {
    try {
        int v = co_await might_fail();
    } catch (std::runtime_error const& e) {
        // handle error
    }
}

// Or from synchronous code:
try {
    sync_wait(might_fail());
} catch (std::runtime_error const& e) {
    // handle error
}
```

### Move-only results

`task<T>` works with move-only types like `std::unique_ptr`:

```cpp
task<std::unique_ptr<int>> make_ptr() {
    co_return std::make_unique<int>(42);
}

auto ptr = sync_wait(make_ptr()); // std::unique_ptr<int>
```

## `sync_wait` -- Blocking Bridge

`sync_wait` runs a task on the calling thread and blocks until it completes.
This is the primary way to consume a `task<T>` from non-coroutine code (e.g.
`main`):

```cpp
int main() {
    // Returns the value
    int result = sync_wait(compute(21));

    // void overload
    sync_wait(log_message("done"));
}
```

> **Note:** `sync_wait` resumes the entire coroutine chain on the calling
> thread. It is intended for top-level entry points. Avoid calling `sync_wait`
> from inside a coroutine -- use `co_await` instead.

## `generator<T>` -- Lazy Sequence Coroutine

A `generator<T>` produces a (potentially infinite) sequence of values
on-demand via `co_yield`. It is compatible with range-based `for` loops.

### Basic usage

```cpp
generator<int> iota(int start, int end) {
    for (int i = start; i < end; ++i)
        co_yield i;
}

for (int v : iota(0, 5)) {
    std::cout << v << " ";   // 0 1 2 3 4
}
```

### Infinite sequences

Generators can represent infinite sequences -- just `break` out of the loop
when you're done. The generator's destructor cleans up the coroutine frame:

```cpp
generator<int> fibonacci() {
    int a = 0, b = 1;
    while (true) {
        co_yield a;
        auto tmp = a;
        a = b;
        b = tmp + b;
    }
}

for (int v : fibonacci()) {
    if (v > 1000) break;
    std::cout << v << "\n";
}
```

### String and complex types

```cpp
generator<std::string> lines(std::istream& is) {
    std::string line;
    while (std::getline(is, line))
        co_yield line;
}

for (auto& line : lines(std::cin)) {
    process(line);
}
```

### Exception propagation

If the generator body throws, the exception is re-thrown on the next iterator
increment:

```cpp
generator<int> might_throw() {
    co_yield 1;
    throw std::runtime_error("generator error");
    co_yield 2; // never reached
}

try {
    for (int v : might_throw()) {
        std::cout << v << "\n"; // prints 1, then throws
    }
} catch (std::runtime_error const& e) {
    // handle error
}
```

### C++23 std::generator detection

When your compiler provides `std::generator` (detected via
`__cpp_lib_generator >= 202207L`), `threadschedule::generator<T>` is
automatically aliased to `std::generator<T>`. No code changes needed -- the
API is compatible.

## Combining Coroutines with Thread Pools

While the coroutine primitives are standalone, they compose naturally with the
library's thread pools:

```cpp
#include <threadschedule/threadschedule.hpp>
using namespace threadschedule;

task<int> compute_on_pool(HighPerformancePool& pool) {
    auto future = pool.submit([]() { return expensive_work(); });
    co_return future.get();
}

int main() {
    HighPerformancePool pool(4);
    int result = sync_wait(compute_on_pool(pool));
}
```

## API Summary

| Type | Header | Description |
|------|--------|-------------|
| `task<T>` | `task.hpp` | Lazy single-value coroutine |
| `task<void>` | `task.hpp` | Lazy void coroutine |
| `sync_wait(task<T>)` | `task.hpp` | Blocking bridge, returns `T` |
| `sync_wait(task<void>)` | `task.hpp` | Blocking bridge, void overload |
| `generator<T>` | `generator.hpp` | Lazy multi-value sequence coroutine |

All types live in `namespace threadschedule` (alias `ts`).

## Design Notes

- **Lazy by default**: Both `task` and `generator` use `suspend_always` for
  `initial_suspend`, so no work happens until the coroutine is consumed.
- **Symmetric transfer**: `task` uses symmetric transfer in `final_suspend` to
  resume the parent coroutine without stack overflow on deep call chains.
- **Move-only**: Both `task` and `generator` own their coroutine handle and
  are move-only (no copies).
- **Zero dynamic allocation** beyond the compiler-generated coroutine frame.
