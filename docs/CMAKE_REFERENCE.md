# ThreadSchedule CMake Configuration Reference

## CMake Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `THREADSCHEDULE_BUILD_EXAMPLES` | BOOL | ON (main project)<br>OFF (subdirectory) | Build example programs |
| `THREADSCHEDULE_BUILD_TESTS` | BOOL | OFF | Build unit tests |
| `THREADSCHEDULE_BUILD_BENCHMARKS` | BOOL | OFF | Build benchmarks (downloads Google Benchmark) |
| `THREADSCHEDULE_RUNTIME` | BOOL | OFF | Build shared runtime library for process-wide registry |
| `THREADSCHEDULE_INSTALL` | BOOL | ON (main project)<br>OFF (subdirectory) | Generate install targets |

## CMake Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `CMAKE_CXX_STANDARD` | C++ standard version | 17, 20, or 23 |
| `CMAKE_CXX_STANDARD_REQUIRED` | Enforce C++ standard | ON/OFF |
| `CMAKE_INSTALL_PREFIX` | Installation directory | `/usr/local` or `$HOME/.local` |

## C++ Standard Support

ThreadSchedule automatically adapts to your project's C++ standard:

### C++17 (Minimum)
```cmake
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(ThreadSchedule)
```
Features: ThreadWrapper, PThreadWrapper, thread pools, scheduling

### C++20
```cmake
set(CMAKE_CXX_STANDARD 20)
add_subdirectory(ThreadSchedule)
```
Features: All C++17 features + JThreadWrapper with stop tokens

### C++23 (Recommended)
```cmake
set(CMAKE_CXX_STANDARD 23)
add_subdirectory(ThreadSchedule)
```
Features: All features + latest language enhancements

## Usage Examples

### Minimal Integration (Default)
```cmake
cmake_minimum_required(VERSION 3.14)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
add_subdirectory(external/ThreadSchedule)

add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE ThreadSchedule::ThreadSchedule)
```

**Result**: Only ThreadSchedule headers are included. No examples, tests, or benchmarks are built.

### With Examples
```cmake
set(THREADSCHEDULE_BUILD_EXAMPLES ON)
add_subdirectory(external/ThreadSchedule)
```

**Result**: Example programs are built in addition to the library.

### With Tests
```cmake
set(THREADSCHEDULE_BUILD_TESTS ON)
add_subdirectory(external/ThreadSchedule)
```

**Result**: Unit tests are built and can be run with `ctest`.

### With Benchmarks
```cmake
set(THREADSCHEDULE_BUILD_BENCHMARKS ON)
add_subdirectory(external/ThreadSchedule)
```

**Result**: Benchmark programs are built. Google Benchmark is automatically downloaded via CPM.

### With Shared Runtime
```cmake
set(THREADSCHEDULE_RUNTIME ON)
add_subdirectory(external/ThreadSchedule)

add_library(mylib SHARED src/mylib.cpp)
target_link_libraries(mylib PRIVATE 
    ThreadSchedule::ThreadSchedule
    ThreadSchedule::Runtime
)

add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE 
    ThreadSchedule::ThreadSchedule
    ThreadSchedule::Runtime
    mylib
)
```

**Result**: A shared runtime library (`libthreadschedule.so` / `threadschedule.dll`) is built. All components share a single process-wide registry instance.

### Development Build (All Features)
```cmake
set(THREADSCHEDULE_BUILD_EXAMPLES ON)
set(THREADSCHEDULE_BUILD_TESTS ON)
set(THREADSCHEDULE_BUILD_BENCHMARKS ON)
set(THREADSCHEDULE_RUNTIME ON)
add_subdirectory(external/ThreadSchedule)
```

**Result**: Everything is built, including the shared runtime.

### Custom Installation
```cmake
cmake_minimum_required(VERSION 3.14)
project(ThreadSchedule VERSION 1.0.0 LANGUAGES CXX)

# ... setup ...

# For system-wide installation
cmake -B build -DTHREADSCHEDULE_INSTALL=ON -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
sudo cmake --install build

# For user installation
cmake -B build -DTHREADSCHEDULE_INSTALL=ON -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build
cmake --install build
```

## Target Properties

### ThreadSchedule::ThreadSchedule

The main interface target. Properties:

- **Type**: INTERFACE library (header-only by default)
- **Include directories**: `include/`
- **Required C++ standard**: C++17 minimum
- **Linked libraries**: `Threads::Threads` (and `pthread`, `rt` on Linux)

### Usage
```cmake
target_link_libraries(your_target PRIVATE ThreadSchedule::ThreadSchedule)
```

### ThreadSchedule::Runtime

Optional shared runtime target for process-wide registry. Properties:

- **Type**: SHARED library (DLL/SO)
- **Availability**: Only when `THREADSCHEDULE_RUNTIME=ON`
- **Include directories**: `include/`
- **Exports**: `registry()` and `set_external_registry()` functions
- **Use case**: Multi-DSO applications requiring single registry instance

### Usage
```cmake
# Enable runtime build
cmake -B build -DTHREADSCHEDULE_RUNTIME=ON

# Link against runtime
target_link_libraries(your_target PRIVATE 
    ThreadSchedule::ThreadSchedule
    ThreadSchedule::Runtime
)
```

**Note**: When using the runtime library, all DSOs (libraries and executables) in your application must link against `ThreadSchedule::Runtime` to share the same registry instance.

## Platform-Specific Behavior

### Linux
```cmake
# Header-only mode: Automatically links: pthread, rt
target_link_libraries(ThreadSchedule INTERFACE Threads::Threads pthread rt)

# Runtime mode: Exports symbols from libthreadschedule.so
# Make sure libthreadschedule.so is in LD_LIBRARY_PATH or use rpath
```

### Windows
```cmake
# Header-only mode: Automatically links: standard thread library
target_link_libraries(ThreadSchedule INTERFACE Threads::Threads)

# Runtime mode: Creates threadschedule.dll
# DLL must be in PATH or same directory as executable
# Integration tests automatically copy DLLs on Windows
```

### macOS
```cmake
# Header-only mode: Automatically links: standard thread library
target_link_libraries(ThreadSchedule INTERFACE Threads::Threads)

# Runtime mode: Creates libthreadschedule.dylib
# Make sure library is in DYLD_LIBRARY_PATH or use rpath
```

## Cross-Compilation

### Linux to Windows (MinGW)
```bash
# Create toolchain file
cat > mingw-toolchain.cmake << 'EOF'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
EOF

# Build
cmake -B build -DCMAKE_TOOLCHAIN_FILE=mingw-toolchain.cmake
cmake --build build
```

### ARM Cross-Compilation
```bash
# Create toolchain file
cat > arm-toolchain.cmake << 'EOF'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
EOF

# Build
cmake -B build -DCMAKE_TOOLCHAIN_FILE=arm-toolchain.cmake
cmake --build build
```

## Troubleshooting

### CMake Version Too Old
```
CMake Error: CMake 3.14 or higher is required.
```
**Solution**: Upgrade CMake to version 3.14 or higher. ThreadSchedule requires modern CMake features for proper dependency management and cross-platform support.

### C++ Standard Too Old
```
Error: ThreadSchedule requires at least C++17
```
**Solution**: Set `CMAKE_CXX_STANDARD` to 17 or higher.

### pthread Not Found (Windows)
This shouldn't happen with the updated CMake files. If it does:
**Solution**: Ensure you're using MSVC or MinGW-w64 compiler.

### Examples/Tests Not Building
**Solution**: Explicitly enable them:
```cmake
set(THREADSCHEDULE_BUILD_EXAMPLES ON)
set(THREADSCHEDULE_BUILD_TESTS ON)
```

### Install Target Not Available
**Solution**: Enable installation:
```cmake
cmake .. -DTHREADSCHEDULE_INSTALL=ON
```

### Runtime DLL Not Found (Windows)
```
Error: The code execution cannot proceed because threadschedule.dll was not found.
Exit code: 0xc0000135
```
**Solution**: Ensure `threadschedule.dll` is in the same directory as your executable or in PATH. For testing, add a post-build copy command:
```cmake
if(WIN32)
    add_custom_command(TARGET your_target POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ThreadSchedule::ThreadScheduleRuntime>
            $<TARGET_FILE_DIR:your_target>
    )
endif()
```

### Multiple Registry Instances in Multi-DSO Setup
**Symptom**: Each shared library has its own thread registry instead of sharing one.

**Cause**: In header-only mode, each DSO gets its own copy of the static `registry_storage()` function.

**Solution**: Either:
1. Use `THREADSCHEDULE_RUNTIME=ON` to build a shared runtime (recommended for multi-DSO)
2. Explicitly inject the registry into each DSO via `set_external_registry()`

## Advanced Configuration

### Custom Compiler Flags (Top-Level Project Only)
```cmake
# ThreadSchedule automatically adds warning flags when built as top-level project
# For subdirectory builds, it doesn't add flags to avoid polluting parent project
```

### Disable Compile Commands Export
```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS OFF)
add_subdirectory(ThreadSchedule)
```

### Build Types
```bash
# Debug build
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Release build
cmake -B build -DCMAKE_BUILD_TYPE=Release

# RelWithDebInfo
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

## Integration with Package Managers

### CPM
```cmake
include(cmake/CPM.cmake)
CPMAddPackage(
    NAME ThreadSchedule
    GITHUB_REPOSITORY Katze719/ThreadSchedule
    VERSION 1.0.0
    OPTIONS
        "THREADSCHEDULE_BUILD_EXAMPLES OFF"
        "THREADSCHEDULE_BUILD_TESTS OFF"
)
```

### FetchContent
```cmake
include(FetchContent)
FetchContent_Declare(
    ThreadSchedule
    GIT_REPOSITORY https://github.com/Katze719/ThreadSchedule.git
    GIT_TAG v1.0.0
)
FetchContent_MakeAvailable(ThreadSchedule)
```

### Conan
See [conanfile.py](../conanfile.py) for Conan package definition.

```bash
conan create . --build=missing
```

## Best Practices

1. **Always specify C++ standard** in your project
2. **Use ThreadSchedule::ThreadSchedule target** (not just ThreadSchedule)
3. **Disable optional components** when using as subdirectory
4. **Use FetchContent or CPM** for automatic dependency management
5. **Pin to specific version** (tag) in production
6. **Test with your target C++ standard** before deploying
7. **For multi-DSO applications**: Use `THREADSCHEDULE_RUNTIME=ON` to ensure single registry
8. **On Windows with runtime**: Copy DLLs to executable directory or use install(RUNTIME_DEPENDENCY_SET)

## Example Project Structure

```
MyProject/
├── CMakeLists.txt          # Your project config
├── src/
│   └── main.cpp
├── external/               # Dependencies
│   └── ThreadSchedule/     # ThreadSchedule repository
└── build/                  # Build directory
```

**CMakeLists.txt**:
```cmake
cmake_minimum_required(VERSION 3.14)
project(MyProject VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(external/ThreadSchedule)

add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE ThreadSchedule::ThreadSchedule)
```

This is the recommended project structure for using ThreadSchedule.
