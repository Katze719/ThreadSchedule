## Thread Registry Usage Guide

This guide explains how to use the process-wide thread registry in common scenarios, including multiple shared libraries (DSOs) using ThreadSchedule.

### What is the registry?

The registry provides a process-wide view of running threads and APIs to control them (affinity, priority, scheduling policy, name). It is header-only by default (with an optional shared runtime), opt-in, and compatible with both Linux and Windows.

- Core entrypoints:
  - `threadschedule::registry()` – default global registry
  - `threadschedule::set_external_registry(...)` – app-injected global registry
  - `threadschedule::CompositeThreadRegistry` – merge multiple registries (views)
  - `threadschedule::AutoRegisterCurrentThread` – RAII auto-registration
  - `threadschedule::ThreadWrapperReg`, `JThreadWrapperReg`, `PThreadWrapperReg` – opt-in wrappers that auto-register

### Architecture Overview

The following diagram shows the object relationships and data flow when using `ThreadWrapperReg` (similar for `JThreadWrapperReg` and `PThreadWrapperReg`):

```mermaid
graph TB
    subgraph "User Code"
        User["User creates:<br/>ThreadWrapperReg(name, tag, func)"]
    end
    
    subgraph "ThreadWrapperReg Class"
        TWR["ThreadWrapperReg<br/>(inherits ThreadWrapper)"]
        Lambda["Wraps user function with:<br/>lambda capture [name, tag, func]"]
    end
    
    subgraph "New Thread Execution"
        Start["Thread starts"]
        Guard["AutoRegisterCurrentThread<br/>guard(name, tag)<br/>(RAII - constructed)"]
        Exec["Execute user function"]
        Cleanup["AutoRegisterCurrentThread<br/>destructor called"]
    end
    
    subgraph "ThreadControlBlock"
        TCB["ThreadControlBlock<br/>• tid (OS thread ID)<br/>• std_id (std::thread::id)<br/>• name (logical name)<br/>• componentTag (grouping)<br/>• handle (HANDLE/pthread_t)"]
        Create["create_for_current_thread()<br/>captures thread info"]
    end
    
    subgraph "ThreadRegistry (Process-Wide)"
        Registry["ThreadRegistry<br/>map&lt;Tid, RegisteredThreadInfo&gt;"]
        RegInfo["RegisteredThreadInfo<br/>• tid<br/>• stdId<br/>• name<br/>• componentTag<br/>• alive<br/>• weak_ptr&lt;ThreadControlBlock&gt;"]
    end
    
    subgraph "Global Access"
        GlobalReg["registry()<br/>returns global singleton"]
        ExtReg["Optional:<br/>set_external_registry(ptr)"]
    end
    
    User -->|constructs| TWR
    TWR -->|creates| Lambda
    Lambda -->|spawns| Start
    Start -->|first action| Guard
    Guard -->|creates| Create
    Create -->|returns shared_ptr| TCB
    Guard -->|registers with| Registry
    TCB -->|stored as weak_ptr in| RegInfo
    RegInfo -->|stored in map| Registry
    Guard -->|continues to| Exec
    Exec -->|completes| Cleanup
    Cleanup -->|unregisters from| Registry
    
    GlobalReg -.->|provides access to| Registry
    ExtReg -.->|can override| GlobalReg
    
    subgraph "Control Operations"
        Control["Application can call:<br/>registry().set_priority(tid, prio)<br/>registry().set_affinity(tid, affinity)<br/>registry().set_name(tid, name)<br/>registry().for_each(callback)<br/>registry().apply(predicate, action)"]
    end
    
    Registry -->|enables| Control
    RegInfo -->|uses weak_ptr to| TCB
    TCB -->|provides control via| Control
    
    style User fill:#e1f5ff
    style TWR fill:#fff4e1
    style Guard fill:#e8f5e9
    style TCB fill:#fff3e0
    style Registry fill:#f3e5f5
    style RegInfo fill:#f3e5f5
    style Control fill:#e0f2f1
    style GlobalReg fill:#fce4ec
    style ExtReg fill:#fce4ec
```

**Key Points:**

1. **ThreadWrapperReg** wraps the user's function in a lambda that creates an `AutoRegisterCurrentThread` guard
2. **AutoRegisterCurrentThread** (RAII) automatically registers the thread on construction and unregisters on destruction
3. **ThreadControlBlock** holds OS-level thread handles and metadata, enabling control operations
4. **RegisteredThreadInfo** is stored in the registry and holds a `weak_ptr` to the control block
5. **ThreadRegistry** maintains a map of all registered threads, indexed by thread ID (Tid)
6. **registry()** provides global access to the default registry instance
7. Applications can query and control threads via `registry().for_each()`, `registry().apply()`, and direct control methods

### When to use which pattern?

- Single app, single shared ThreadSchedule: Use `registry()` directly. Create `*Reg` threads or use `AutoRegisterCurrentThread` in worker entry.
- App with multiple DSOs that also include ThreadSchedule:
  - Preferred: Ensure all components link against the same `libthreadschedule` (shared). `registry()` resolves to the same instance.
  - If components statically include ThreadSchedule: Use `set_external_registry(&appRegistry)` in `main()` and register threads to that instance everywhere.
  - If isolated registries are desired for components: Each component uses its own `ThreadRegistry`, and the app merges them using `CompositeThreadRegistry`.

### Header-only builds and multiple DSOs

Because ThreadSchedule is header-only, each DSO that includes it may get its own internal `registry()` singleton. To obtain a unified process-wide view, use one of these patterns:

- App injection (unify to a single app-owned registry)
  - The app creates a `ThreadRegistry appReg;` and injects it into itself and into every DSO using a small setter exported by each DSO.

DSO side (libX):

```cpp
// libX_api.cpp
#include <threadschedule/thread_registry.hpp>
using namespace threadschedule;

// Allow the host application to inject a registry pointer
void libX_set_registry(ThreadRegistry* reg) {
  set_external_registry(reg);
}
```

App side:

```cpp
#include <threadschedule/thread_registry.hpp>
using namespace threadschedule;

// From each DSO's public header
void libA_set_registry(ThreadRegistry*);
void libB_set_registry(ThreadRegistry*);

int main() {
  ThreadRegistry appReg;
  // App uses the same registry for its own calls
  set_external_registry(&appReg);
  // Propagate the same pointer into all DSOs
  libA_set_registry(&appReg);
  libB_set_registry(&appReg);
  // ... start threads in app and DSOs ...
}
```

- Composite merge (keep DSOs isolated and merge views in the app)
  - Each DSO exposes `ThreadRegistry& libX_registry();` and registers its threads into that local registry. The app builds a `CompositeThreadRegistry` and attaches all of them. See example below.

- Dynamic discovery (no headers)
  - On POSIX, you can `dlsym` exported symbols (e.g., `libX_registry`, `libX_set_registry`) from each DSO at runtime and call them to either attach or inject a registry pointer.

### Single shared runtime (optional, non header-only mode)

If you prefer a single, process-wide registry without app-side injection or composite merging, enable the runtime option. This builds a shared library that owns the global registry and exports the required symbols.

- Enable in CMake:

```bash
cmake -B build -DTHREADSCHEDULE_RUNTIME=ON
cmake --build build
```

- Link your app and DSOs against the runtime target:

```cmake
target_link_libraries(your_app PRIVATE ThreadSchedule::ThreadSchedule ThreadSchedule::Runtime)
target_link_libraries(your_dso PRIVATE ThreadSchedule::ThreadSchedule ThreadSchedule::Runtime)
```

- Exported APIs (same as header-only), provided by the runtime:
  - `threadschedule::registry()` – returns the single process-wide registry instance
  - `threadschedule::set_external_registry(ThreadRegistry*)` – optionally redirect runtime to an app-owned instance

Notes:
- With `THREADSCHEDULE_RUNTIME=ON`, the header declares these functions and the `.so/.dll` provides the definitions.
- This ensures all components in the process resolve to the same registry object as long as they link to the runtime.
- You can still call `set_external_registry(&appReg)` early in `main()` to make the app’s instance authoritative.

### Examples

#### 1) Basic app usage with the default registry

```cpp
#include <threadschedule/registered_threads.hpp>
#include <threadschedule/thread_registry.hpp>
using namespace threadschedule;

int main() {
  ThreadWrapperReg t("worker-1","io", []{ /* work */ });
  registry().apply(
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

In each DSO, define and expose a local `ThreadRegistry` accessor. Threads inside the DSO register to that local registry.

libA (compiled into `libA.so`):

```cpp
// libA.cpp
#include <threadschedule/thread_registry.hpp>
#include <threadschedule/thread_wrapper.hpp>
using namespace threadschedule;

static ThreadRegistry regA;

// exported accessor (C++ symbol if you ship a header)
ThreadRegistry& libA_registry() {
  return regA;
}

// start function used by the app
void libA_start() {
  ThreadWrapper t([]{
    AutoRegisterCurrentThread guard(regA, "a-1","A");
    // ... work ...
  });
  t.detach();
}
```

libB (compiled into `libB.so`) is analogous:

```cpp
// libB.cpp
#include <threadschedule/thread_registry.hpp>
#include <threadschedule/thread_wrapper.hpp>
using namespace threadschedule;

static ThreadRegistry regB;

ThreadRegistry& libB_registry() {
  return regB;
}

void libB_start() {
  ThreadWrapper t([]{
    AutoRegisterCurrentThread guard(regB, "b-1","B");
    // ... work ...
  });
  t.detach();
}
```

In the app, include the DSOs' headers (recommended) and merge via `CompositeThreadRegistry`:

```cpp
// app.cpp
#include <threadschedule/thread_registry.hpp>
using namespace threadschedule;

// from libA_api.hpp and libB_api.hpp provided by the DSOs
ThreadRegistry& libA_registry();
ThreadRegistry& libB_registry();
void libA_start();
void libB_start();

int main() {
  libA_start();
  libB_start();

  CompositeThreadRegistry composite;
  composite.attach(&libA_registry());
  composite.attach(&libB_registry());

  composite.apply(
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

#### 5) Runtime (shared) example – app + two DSOs

This repository includes a minimal working example under `examples/runtime_shared/` that demonstrates using `THREADSCHEDULE_RUNTIME`:

- Targets: `runtime_libA` (DSO), `runtime_libB` (DSO), `runtime_main` (app)
- All are linked against `ThreadSchedule::Runtime` so they share one process-wide registry.

Key snippets:

libA (`examples/runtime_shared/libA.cpp`):

```cpp
#include <threadschedule/thread_registry.hpp>
#include <threadschedule/thread_wrapper.hpp>
extern "C" void libA_start() {
  threadschedule::ThreadWrapper t([]{
    threadschedule::AutoRegisterCurrentThread guard("rt-a1","A");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  });
  t.detach();
}
```

libB is analogous.

App (`examples/runtime_shared/main.cpp`):

```cpp
#include <threadschedule/thread_registry.hpp>
extern "C" void libA_start();
extern "C" void libB_start();
int main(){
  libA_start();
  libB_start();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  int count = 0;
  threadschedule::registry().for_each([&](const threadschedule::RegisteredThreadInfo&){ count++; });
  return count > 0 ? 0 : 1;
}
```

Build:

```bash
cmake -B build -DTHREADSCHEDULE_RUNTIME=ON -DTHREADSCHEDULE_BUILD_EXAMPLES=ON
cmake --build build --target runtime_main
```

Run `runtime_main` – it will list threads from both DSOs via the single shared registry.

### Platform notes

- Linux: Control uses `pthread_*` where control blocks are present; fallback is direct `sched_*` and `/proc/self/task/<tid>/comm`.
- Windows: Control uses a duplicated `HANDLE` where available; fallback opens a thread handle via `OpenThread`.

### Error handling

All control functions return `expected<void, std::error_code>`. Typical errors include `ESRCH` (no such process/thread), `EPERM` (insufficient privileges), and `EINVAL` (invalid parameters).


