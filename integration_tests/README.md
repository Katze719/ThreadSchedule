# Thread Registry Integration Tests

This directory contains real integration tests for the global thread registry feature, demonstrating how ThreadSchedule works across applications and shared libraries (DSOs/DLLs).

## Overview

These scenarios validate the different registry patterns documented in `docs/REGISTRY.md` in real-world multi-library setups.

## Scenarios

### 1. runtime_single/

**Pattern:** Single process-wide registry via shared runtime

**Setup:**
- Two DSOs (`libA`, `libB`) and a main app
- All link to `ThreadSchedule::Runtime` (the optional shared library)
- All use `threadschedule::registry()` to access the **same** global instance

**What it tests:**
- The `THREADSCHEDULE_RUNTIME` mode provides a single, exported registry instance
- Multiple DSOs + app all see the same registry without explicit injection
- The `THREADSCHEDULE_RUNTIME` INTERFACE definition propagates correctly to consumers

**Build requirement:** `THREADSCHEDULE_RUNTIME=ON`

**Key code:**
```cpp
// In any DSO or app:
AutoRegisterCurrentThread guard(name, "ComponentTag");
// ...
int total = 0;
registry().for_each([&](RegisteredThreadInfo const& info){ total++; });
```

All four threads from both DSOs appear in the single `registry()`.

---

### 2. composite_merge/

**Pattern:** Library-local registries merged via composite view

**Setup:**
- Two DSOs (`libA`, `libB`) each maintain their own **private** `ThreadRegistry`
- Main app does **not** inject or share a registry
- Main app creates a `CompositeThreadRegistry` and attaches both library registries

**What it tests:**
- Libraries can remain isolated with their own registries
- App can enumerate/control threads across all registries using `CompositeThreadRegistry`
- No coupling between DSOs; each uses `AutoRegisterCurrentThread` with a local registry

**Build requirement:** Header-only mode (default)

**Key code:**
```cpp
// In libA: AutoRegisterCurrentThread guard(local_registry, name, "CompositeLibA");
// In libB: AutoRegisterCurrentThread guard(local_registry, name, "CompositeLibB");
// In app:
CompositeThreadRegistry composite;
composite.attach(&libA_registry);
composite.attach(&libB_registry);
composite.for_each([](RegisteredThreadInfo const& info){ /* ... */ });
```

---

### 3. app_injection/

**Pattern:** App-owned registry injected globally

**Setup:**
- Two DSOs (`libA`, `libB`) use the default `registry()` (header-only mode)
- Main app creates its own `ThreadRegistry` instance
- App calls `set_external_registry(&app_registry)` **before** starting threads
- DSOs call `AutoRegisterCurrentThread(...)` (defaults to `registry()`, which now returns the injected instance)

**What it tests:**
- App can inject a custom registry for all DSOs to use
- `set_external_registry()` correctly overrides the default singleton
- All threads from both DSOs register into the app-owned instance

**Build requirement:** Header-only mode (default)

**Key code:**
```cpp
// In app:
ThreadRegistry app_registry;
set_external_registry(&app_registry);
// Start DSO threads...
registry().for_each([](RegisteredThreadInfo const& info){ /* sees all threads */ });
set_external_registry(nullptr); // reset to local
```

## How It Works

These are **true integration tests**, not unit tests:
- Each scenario is a separate CMake project (`cmake_minimum_required(...)` at the top)
- DSOs are built as independent shared libraries
- The main app links against the DSOs and ThreadSchedule
- Tests run via CTest (`add_test(...)`)
- Validates cross-library behavior in realistic deployment scenarios

## Running Locally

### Prerequisites
Install ThreadSchedule to a local prefix (for header-only scenarios):

```bash
cd /home/paul/projects/ThreadSchedule
cmake -B build -DCMAKE_INSTALL_PREFIX=/tmp/ts-install
cmake --build build
cmake --install build
```

For `runtime_single`, install with runtime enabled:

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=/tmp/ts-install -DTHREADSCHEDULE_RUNTIME=ON
cmake --build build
cmake --install build
```

### Run app_injection

```bash
cd integration_tests/app_injection
cmake -B build -DCMAKE_PREFIX_PATH=/tmp/ts-install
cmake --build build
ctest --test-dir build --output-on-failure
```

### Run composite_merge

```bash
cd integration_tests/composite_merge
cmake -B build -DCMAKE_PREFIX_PATH=/tmp/ts-install
cmake --build build
ctest --test-dir build --output-on-failure
```

### Run runtime_single

```bash
cd integration_tests/runtime_single
cmake -B build -DTHREADSCHEDULE_RUNTIME=ON -DCMAKE_PREFIX_PATH=/tmp/ts-install
cmake --build build
export LD_LIBRARY_PATH=/tmp/ts-install/lib64:$LD_LIBRARY_PATH  # or lib on some systems
ctest --test-dir build --output-on-failure
```

## CI Coverage

All three scenarios run in CI on:
- **Linux x86_64** (`ubuntu-latest`)
- **Linux ARM64** (`ubuntu-24.04-arm`)
- **Windows** (`windows-latest` with MSVC)

See `.github/workflows/registry-integration.yml` for the full CI configuration.

## What's Validated

- **Cross-library registration:** Threads in DSO A and DSO B register correctly
- **Registry unification:** Different patterns to achieve a single view or multiple isolated views
- **Thread-safety:** Concurrent registration/enumeration across libraries
- **Platform portability:** Windows (with DLL export macros) and Linux (ELF symbol visibility)
- **Real linking:** Actual `find_package(ThreadSchedule)` usage, not toy examples
- **Error handling:** Fallback to OS APIs when control blocks expire

## Why This Matters

These tests ensure the registry feature works as documented when:
- Multiple independent libraries use ThreadSchedule
- An application integrates those libraries
- Different deployment scenarios are used (single runtime DLL, header-only with injection, header-only with composite merge)

This validates that the patterns in `docs/REGISTRY.md` are production-ready.
