## Thread Registry Usage Guide

This guide explains how to use the process-wide thread registry in common scenarios, including multiple shared libraries (DSOs) using ThreadSchedule.

### What is the registry?

The registry provides a process-wide view of running threads and APIs to control them (affinity, priority, scheduling policy, name). It is header-only, opt-in, and compatible with both Linux and Windows.

- Core entrypoints:
  - `threadschedule::registry()` – default global registry
  - `threadschedule::set_external_registry(...)` – app-injected global registry
  - `threadschedule::CompositeThreadRegistry` – merge multiple registries (views)
  - `threadschedule::AutoRegisterCurrentThread` – RAII auto-registration
  - `threadschedule::ThreadWrapperReg`, `JThreadWrapperReg`, `PThreadWrapperReg` – opt-in wrappers that auto-register

### When to use which pattern?

- Single app, single shared ThreadSchedule: Use `registry()` directly. Create `*Reg` threads or use `AutoRegisterCurrentThread` in worker entry.
- App with multiple DSOs that also include ThreadSchedule:
  - Preferred: Ensure all components link against the same `libthreadschedule` (shared). `registry()` resolves to the same instance.
  - If components statically include ThreadSchedule: Use `set_external_registry(&appRegistry)` in `main()` and register threads to that instance everywhere.
  - If isolated registries are desired for components: Each component uses its own `ThreadRegistry`, and the app merges them using `CompositeThreadRegistry`.

### Examples

#### 1) Basic app usage with the default registry

```cpp
#include <threadschedule/registered_threads.hpp>
#include <threadschedule/thread_registry.hpp>
using namespace threadschedule;

int main() {
  ThreadWrapperReg t("worker-1","io", []{ /* work */ });
  registry().apply_all(
    [](const RegisteredThreadInfo& e){ return e.componentTag=="io"; },
    [&](const RegisteredThreadInfo& e){ (void)registry().set_priority(e.tid, ThreadPriority{0}); }
  );
  t.join();
}
```

#### 2) App-owned global registry (injection)

```cpp
#include <threadschedule/thread_registry.hpp>
using namespace threadschedule;

int main() {
  ThreadRegistry appReg;
  set_external_registry(&appReg); // all registry() calls now use appReg
  // ... rest of program ...
}
```

Component thread entry (any library):

```cpp
void component_worker() {
  AutoRegisterCurrentThread guard("comp-w","component");
  // ... do work ...
}
```

#### 3) Multiple DSOs with isolated registries, merged by the app

In each DSO, threads register into a local `ThreadRegistry`:

```cpp
// in libA.so
static ThreadRegistry regA;
void start_A() {
  ThreadWrapper t([]{
    AutoRegisterCurrentThread guard(regA, "a-1","A");
    // ... work ...
  });
  t.detach();
}

// in libB.so
static ThreadRegistry regB;
void start_B() {
  ThreadWrapper t([]{
    AutoRegisterCurrentThread guard(regB, "b-1","B");
    // ... work ...
  });
  t.detach();
}
```

In the app, merge registries via `CompositeThreadRegistry`:

```cpp
#include <threadschedule/thread_registry.hpp>
using namespace threadschedule;

extern ThreadRegistry& libA_registry();
extern ThreadRegistry& libB_registry();

void control_all() {
  CompositeThreadRegistry composite;
  composite.attach(&libA_registry());
  composite.attach(&libB_registry());
  composite.apply_all(
    [](const RegisteredThreadInfo&){ return true; },
    [&](const RegisteredThreadInfo& e){ (void)registry().set_priority(e.tid, ThreadPriority{0}); }
  );
}
```

#### 4) Registering foreign threads without using wrappers

```cpp
void foreign_thread() {
  AutoRegisterCurrentThread guard("foreign","misc");
  // ... work ...
}
```

### Platform notes

- Linux: Control uses `pthread_*` where control blocks are present; fallback is direct `sched_*` and `/proc/self/task/<tid>/comm`.
- Windows: Control uses a duplicated `HANDLE` where available; fallback opens a thread handle via `OpenThread`.

### Error handling

All control functions return `expected<void, std::error_code>`. Typical errors include `ESRCH` (no such process/thread), `EPERM` (insufficient privileges), and `EINVAL` (invalid parameters).


