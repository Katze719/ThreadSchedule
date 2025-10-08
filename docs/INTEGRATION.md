# ThreadSchedule Integration Guide

ThreadSchedule is a modern, cross-platform, header-only C++ library for thread management. This guide covers all integration methods.

## Table of Contents

- [Requirements](#requirements)
- [Integration Methods](#integration-methods)
  - [Method 1: CPM.cmake (Recommended)](#method-1-cpmcmake-recommended)
  - [Method 2: CMake FetchContent](#method-2-cmake-fetchcontent)
  - [Method 3: Conan Package Manager](#method-3-conan-package-manager)
  - [Method 4: CMake add_subdirectory](#method-4-cmake-add_subdirectory)
  - [Method 5: System Installation](#method-5-system-installation)
- [C++ Standard Compatibility](#c-standard-compatibility)
- [Cross-Compilation](#cross-compilation)
- [Configuration Options](#configuration-options)

## Requirements

- **CMake**: 3.14 or later
- **C++ Standard**: C++17, C++20, or C++23
- **Compilers**:
  - GCC 10+ or Clang 12+ (Linux/macOS)
  - MSVC 2019+ (Windows)
  - MinGW-w64 (Windows cross-compilation)
- **Platforms**: Linux, Windows, macOS (limited support)

## Integration Methods

### Method 1: CPM.cmake (Recommended)

**Best for**: Most projects - automatic dependency management with caching and version control.

[CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) is a modern CMake dependency manager that combines the best of FetchContent with advanced caching and version management.

**Setup**: Add to your CMakeLists.txt:

```cmake
cmake_minimum_required(VERSION 3.14)
project(YourProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Download CPM.cmake if not already present
if(NOT EXISTS "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
    file(DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.8/CPM.cmake
        ${CMAKE_BINARY_DIR}/cmake/CPM.cmake
    )
endif()
include(${CMAKE_BINARY_DIR}/cmake/CPM.cmake)

# Add ThreadSchedule
CPMAddPackage(
    NAME ThreadSchedule
    GITHUB_REPOSITORY Katze719/ThreadSchedule
    GIT_TAG main  # or specific version tag
    OPTIONS
        "THREADSCHEDULE_BUILD_EXAMPLES OFF"
        "THREADSCHEDULE_BUILD_TESTS OFF"
)

add_executable(your_app src/main.cpp)
target_link_libraries(your_app PRIVATE ThreadSchedule::ThreadSchedule)
```

**Use in your code:**
```cpp
#include <threadschedule/threadschedule.hpp>

int main() {
    using namespace threadschedule;
    
    HighPerformancePool pool(4);
    auto future = pool.submit([]() { return 42; });
    std::cout << "Result: " << future.get() << std::endl;
    
    return 0;
}
```

**Why CPM?**
- ✅ Automatic caching - downloads dependencies once
- ✅ Version pinning - reproducible builds
- ✅ No git submodules needed
- ✅ Works seamlessly with CI/CD
- ✅ Compatible with all CMake features

### Method 2: CMake FetchContent

**Best for**: Projects that download dependencies automatically.

```cmake
cmake_minimum_required(VERSION 3.14)
project(YourProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include FetchContent module
include(FetchContent)

# Fetch ThreadSchedule
FetchContent_Declare(
    ThreadSchedule
    GIT_REPOSITORY https://github.com/Katze719/ThreadSchedule.git
    GIT_TAG v1.0.0  # or main for latest
)

# Make ThreadSchedule available
FetchContent_MakeAvailable(ThreadSchedule)

# Your executable
add_executable(your_app src/main.cpp)

# Link ThreadSchedule
target_link_libraries(your_app PRIVATE ThreadSchedule::ThreadSchedule)
```

### Method 3: Conan Package Manager

**Best for**: Projects using Conan package manager.

**Step 1**: Create conanfile.txt in your project
```ini
[requires]
threadschedule/1.0.0

[generators]
CMakeDeps
CMakeToolchain

[options]
threadschedule:build_examples=False
threadschedule:build_tests=False
```

**Step 2**: Install dependencies
```bash
conan install . --output-folder=build --build=missing
```

**Step 3**: Configure your CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.14)
project(YourProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Use Conan toolchain
include(${CMAKE_BINARY_DIR}/conan_toolchain.cmake)

find_package(ThreadSchedule REQUIRED)

add_executable(your_app src/main.cpp)
target_link_libraries(your_app PRIVATE ThreadSchedule::ThreadSchedule)
```

**Step 4**: Build your project
```bash
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Method 4: CMake add_subdirectory

**Best for**: Projects with dependencies as subdirectories or git submodules.

**Step 1**: Add ThreadSchedule to your project
```bash
# Clone into your project
cd your_project
mkdir -p external
cd external
git clone https://github.com/Katze719/ThreadSchedule.git
```

**Step 2**: Add to your CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.14)
project(YourProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add ThreadSchedule
add_subdirectory(external/ThreadSchedule)

add_executable(your_app src/main.cpp)
target_link_libraries(your_app PRIVATE ThreadSchedule::ThreadSchedule)
```

### Method 5: System Installation

**Best for**: System-wide installation or package maintainers.

**Step 1**: Clone and install
```bash
git clone https://github.com/Katze719/ThreadSchedule.git
cd ThreadSchedule
mkdir build && cd build

cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local -DTHREADSCHEDULE_INSTALL=ON
sudo cmake --install .
```

**Step 2**: Use in your CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.14)
project(YourProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Find installed ThreadSchedule
find_package(ThreadSchedule REQUIRED)

add_executable(your_app src/main.cpp)
target_link_libraries(your_app PRIVATE ThreadSchedule::ThreadSchedule)
```

## C++ Standard Compatibility

ThreadSchedule supports C++17, C++20, and C++23:

### C++17 (Minimum)
```cmake
set(CMAKE_CXX_STANDARD 17)
```
- Full ThreadWrapper support
- Full PThreadWrapper support (Linux only)
- Thread pools and scheduling

### C++20
```cmake
set(CMAKE_CXX_STANDARD 20)
```
- All C++17 features
- JThreadWrapper with stop tokens
- Enhanced thread management

### C++23 (Recommended)
```cmake
set(CMAKE_CXX_STANDARD 23)
```
- All C++20 features
- Latest language features
- Best performance

**Note**: The library will automatically adapt based on your project's C++ standard. You don't need to specify anything special in ThreadSchedule.

## Cross-Compilation

### Linux to Windows (MinGW)

**Step 1**: Install MinGW toolchain
```bash
sudo apt-get install mingw-w64 g++-mingw-w64
```

**Step 2**: Create toolchain file (mingw-toolchain.cmake)
```cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

**Step 3**: Build with toolchain
```bash
mkdir build-mingw
cd build-mingw
cmake .. -DCMAKE_TOOLCHAIN_FILE=../mingw-toolchain.cmake
cmake --build .
```

### Native MinGW Build on Windows

**Step 1**: Install MinGW-w64 via MSYS2
```bash
# Download and install MSYS2 from https://www.msys2.org/
# Then in MSYS2 terminal:
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
```

**Step 2**: Build with MinGW
```bash
# In MSYS2 MinGW64 shell:
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Notes**:
- MinGW-w64 provides full Windows API support including thread naming (Windows 10+)
- All ThreadSchedule features work with MinGW, including `SetThreadDescription`/`GetThreadDescription`
- The library automatically sets `_WIN32_WINNT=0x0A00` for MinGW to enable Windows 10 APIs
- Use the MSYS2 MinGW64 shell, not the MSYS shell, for native Windows builds

### ARM Cross-Compilation

**Example**: Building for Raspberry Pi
```cmake
# arm-toolchain.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
```

Build:
```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=../arm-toolchain.cmake
cmake --build .
```

## Configuration Options

ThreadSchedule provides several CMake options:

| Option | Default | Description |
|--------|---------|-------------|
| `THREADSCHEDULE_BUILD_EXAMPLES` | ON (main project)<br>OFF (subdirectory) | Build example programs |
| `THREADSCHEDULE_BUILD_TESTS` | OFF | Build unit tests |
| `THREADSCHEDULE_BUILD_BENCHMARKS` | OFF | Build benchmarks |
| `THREADSCHEDULE_INSTALL` | ON (main project)<br>OFF (subdirectory) | Generate install targets |

### Example: Enable all options
```cmake
set(THREADSCHEDULE_BUILD_EXAMPLES ON)
set(THREADSCHEDULE_BUILD_TESTS ON)
set(THREADSCHEDULE_BUILD_BENCHMARKS ON)

add_subdirectory(external/ThreadSchedule)
```

### Example: Custom install location
```bash
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local -DTHREADSCHEDULE_INSTALL=ON
cmake --install .
```

## Platform-Specific Notes

### Linux
- Full pthread support
- All features available
- Recommended for production use

### Windows
- ThreadWrapper and JThreadWrapper fully supported
- PThreadWrapper not available (use ThreadWrapper instead)
- Use MSVC or MinGW-w64 compiler

### macOS
- Basic support
- Some features may be limited
- Testing on macOS is limited

## Troubleshooting

### C++ Standard Errors
```
Error: ThreadSchedule requires at least C++17
```
**Solution**: Set `CMAKE_CXX_STANDARD` to 17 or higher in your project.

### Threading Errors on Linux
```
undefined reference to pthread_create
```
**Solution**: ThreadSchedule automatically links pthread on Linux. Make sure you're using `target_link_libraries()` correctly.

### CPM Download Issues
**Solution**: If CPM.cmake download fails, manually download it:
```bash
wget https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.8/CPM.cmake
# Place in your cmake/ directory
```

### Conan Package Not Found
**Solution**: Build from source:
```bash
conan create . --build=missing
```

### MinGW: Missing Windows APIs
```
undefined reference to `SetThreadDescription'
```
**Solution**: Ensure you're using a recent MinGW-w64 version (8.0+) and that `_WIN32_WINNT` is set to 0x0A00 or higher. ThreadSchedule does this automatically, but if you override compiler flags, you may need to add:
```cmake
target_compile_definitions(your_target PRIVATE _WIN32_WINNT=0x0A00)
```

### MinGW: Wrong Shell
**Problem**: Build fails with strange errors on Windows with MinGW
**Solution**: Make sure you're using the MSYS2 **MinGW64** shell (not MSYS or UCRT64) when building with MinGW-w64.

## Complete Integration Example

Here's a complete example project structure:

```
MyProject/
├── CMakeLists.txt
├── src/
│   └── main.cpp
└── external/
    └── ThreadSchedule/  (or use FetchContent/CPM)
```

**CMakeLists.txt**:
```cmake
cmake_minimum_required(VERSION 3.14)
project(MyProject VERSION 1.0.0 LANGUAGES CXX)

# C++20 for modern features
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option 1: subdirectory
add_subdirectory(external/ThreadSchedule)

# Option 2: FetchContent
# include(FetchContent)
# FetchContent_Declare(ThreadSchedule
#     GIT_REPOSITORY https://github.com/Katze719/ThreadSchedule.git
#     GIT_TAG v1.0.0
# )
# FetchContent_MakeAvailable(ThreadSchedule)

# Your application
add_executable(my_app src/main.cpp)

target_link_libraries(my_app PRIVATE 
    ThreadSchedule::ThreadSchedule
)

# Optional: compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(my_app PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(my_app PRIVATE /W4)
endif()
```

**src/main.cpp**:
```cpp
#include <threadschedule/threadschedule.hpp>
#include <iostream>

int main() {
    using namespace threadschedule;
    
    // Create high-performance thread pool
    HighPerformancePool pool(4);
    pool.configure_threads("worker");
    
    // Submit tasks
    auto future = pool.submit([]() {
        return 42;
    });
    
    std::cout << "Result: " << future.get() << std::endl;
    
    return 0;
}
```

Build and run:
```bash
mkdir build && cd build
cmake ..
cmake --build .
./my_app
```

## Getting Help

- **Documentation**: See [README.md](../README.md) in the repository
- **Issues**: https://github.com/Katze719/ThreadSchedule/issues
- **Examples**: Check the `examples/` directory in the repository
