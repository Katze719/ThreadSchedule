# ThreadSchedule CMake Configuration Reference

## CMake Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `THREADSCHEDULE_BUILD_EXAMPLES` | BOOL | ON (main project)<br>OFF (subdirectory) | Build example programs |
| `THREADSCHEDULE_BUILD_TESTS` | BOOL | OFF | Build unit tests |
| `THREADSCHEDULE_BUILD_BENCHMARKS` | BOOL | OFF | Build benchmarks (downloads Google Benchmark) |
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

### Development Build (All Features)
```cmake
set(THREADSCHEDULE_BUILD_EXAMPLES ON)
set(THREADSCHEDULE_BUILD_TESTS ON)
set(THREADSCHEDULE_BUILD_BENCHMARKS ON)
add_subdirectory(external/ThreadSchedule)
```

**Result**: Everything is built.

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

- **Type**: INTERFACE library (header-only)
- **Include directories**: `include/`
- **Required C++ standard**: C++17 minimum
- **Linked libraries**: `Threads::Threads` (and `pthread`, `rt` on Linux)

### Usage
```cmake
target_link_libraries(your_target PRIVATE ThreadSchedule::ThreadSchedule)
```

## Platform-Specific Behavior

### Linux
```cmake
# Automatically links: pthread, rt
target_link_libraries(ThreadSchedule INTERFACE Threads::Threads pthread rt)
```

### Windows
```cmake
# Automatically links: standard thread library
target_link_libraries(ThreadSchedule INTERFACE Threads::Threads)
```

### macOS
```cmake
# Automatically links: standard thread library
target_link_libraries(ThreadSchedule INTERFACE Threads::Threads)
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
**Solution**: Upgrade CMake or edit `CMakeLists.txt` to lower the requirement (at your own risk).

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
