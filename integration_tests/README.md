# Thread Registry Integration Tests

This directory contains real integration tests for the global thread registry feature across applications and shared libraries (DSOs/DLLs).

## Structure

The integration tests consist of three separate CMake projects:

1. **library_a/** - A shared library that uses ThreadSchedule
   - Has its own internal `ThreadRegistry`
   - Can accept an external registry via `set_registry()`
   - Creates worker threads that register themselves

2. **library_b/** - A second shared library that uses ThreadSchedule
   - Independent from library_a
   - Also has its own internal registry
   - Can accept an external registry via `set_registry()`

3. **main_app/** - The main application
   - Links against both library_a and library_b
   - Uses only ThreadSchedule headers (header-only)
   - Tests different registry patterns

## Patterns Tested

### 1. Isolated Registries with Composite Merge
Each library maintains its own registry, and the main application creates a `CompositeThreadRegistry` to view all threads across both libraries.

**Use case:** When libraries need registry isolation but the application wants unified visibility.

### 2. App Injection Pattern
The application creates a single `ThreadRegistry` and injects it into both libraries using their `set_registry()` functions. All threads register to this shared registry.

**Use case:** When all components should share a single unified registry.

### 3. Concurrent Operations
Tests that multiple threads from different libraries can safely register to the same registry concurrently.

**Use case:** Validating thread-safety and concurrent access patterns.

## How It Works

The integration test validates that:
- Each library can be built independently with only ThreadSchedule as a dependency
- Libraries with isolated registries work correctly
- Libraries can share a single registry when injected by the application
- `CompositeThreadRegistry` correctly merges views from multiple registries
- Thread registration and discovery works across library boundaries
- The patterns documented in `docs/REGISTRY.md` work in real-world scenarios

## Running Locally

The CI workflow automatically builds and runs these tests, but you can also run them locally:

```bash
# Install ThreadSchedule to a local prefix
cd /path/to/ThreadSchedule
cmake -B build -DCMAKE_INSTALL_PREFIX=/tmp/threadschedule-install
cmake --build build
cmake --install build

# Build library A
cd integration_tests/library_a
cmake -B build -DCMAKE_PREFIX_PATH=/tmp/threadschedule-install
cmake --build build
cmake --install build --prefix /tmp/integration-install

# Build library B
cd ../library_b
cmake -B build -DCMAKE_PREFIX_PATH=/tmp/threadschedule-install
cmake --build build
cmake --install build --prefix /tmp/integration-install

# Build and run main app
cd ../main_app
cmake -B build -DCMAKE_PREFIX_PATH="/tmp/threadschedule-install;/tmp/integration-install"
cmake --build build
./build/main_app
```

## Platform Support

These tests work on:
- **Linux** - Full support
- **Windows** - Full support with DLL export/import macros
- **macOS** - Full support

## Why This Matters

These are **real integration tests**, not unit tests:
- Separate compilation units (libraries built independently)
- Real shared library linking (not dlopen, but actual linking)
- Cross-library communication through headers only
- Validates the header-only nature of ThreadSchedule
- Tests actual deployment scenarios where multiple libraries use ThreadSchedule
