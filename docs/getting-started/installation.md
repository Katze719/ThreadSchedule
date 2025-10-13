# Installation

ThreadSchedule can be integrated into your project using several methods. Choose the one that best fits your workflow.

## Requirements

- **C++ Standard**: C++17 or newer (C++20 and C++23 fully supported)
- **CMake**: Version 3.14 or newer
- **Compiler**: 
  - GCC 9+ (Linux)
  - Clang 10+ (Linux/macOS)
  - MSVC 2019+ (Windows)
  - MinGW-w64 (Windows)

## Method 1: CPM.cmake (Recommended)

CPM.cmake provides the simplest integration with automatic dependency management.

**Step 1**: Download CPM.cmake

```cmake
# Add to your CMakeLists.txt
if(NOT EXISTS "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
    file(
        DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.8/CPM.cmake
        ${CMAKE_BINARY_DIR}/cmake/CPM.cmake
        EXPECTED_HASH SHA256=78ba32abdf798bc616bab7c73aac32a17bbd7b06ad9e26a6add69de8f3ae4791
    )
endif()
include(${CMAKE_BINARY_DIR}/cmake/CPM.cmake)
```

**Step 2**: Add ThreadSchedule

```cmake
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

## Method 2: CMake FetchContent

Built-in CMake solution for downloading dependencies.

```cmake
include(FetchContent)

FetchContent_Declare(
    ThreadSchedule
    GIT_REPOSITORY https://github.com/Katze719/ThreadSchedule.git
    GIT_TAG main  # or specific version tag
)

# Optional: configure options before making available
set(THREADSCHEDULE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(THREADSCHEDULE_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(ThreadSchedule)

add_executable(your_app src/main.cpp)
target_link_libraries(your_app PRIVATE ThreadSchedule::ThreadSchedule)
```

## Method 3: Git Submodule

For projects that use git submodules.

```bash
# Add as submodule
cd your_project
git submodule add https://github.com/Katze719/ThreadSchedule.git external/ThreadSchedule
git submodule update --init --recursive
```

**CMakeLists.txt**:
```cmake
add_subdirectory(external/ThreadSchedule)

add_executable(your_app src/main.cpp)
target_link_libraries(your_app PRIVATE ThreadSchedule::ThreadSchedule)
```

## Method 4: System Installation

Install ThreadSchedule system-wide for use across multiple projects.

```bash
# Clone and build
git clone https://github.com/Katze719/ThreadSchedule.git
cd ThreadSchedule
mkdir build && cd build

# Configure and install
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DTHREADSCHEDULE_BUILD_EXAMPLES=OFF \
         -DTHREADSCHEDULE_BUILD_TESTS=OFF \
         -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build .
sudo cmake --install .
```

**Use in your project**:
```cmake
find_package(ThreadSchedule REQUIRED)

add_executable(your_app src/main.cpp)
target_link_libraries(your_app PRIVATE ThreadSchedule::ThreadSchedule)
```

## Method 5: Conan Package Manager

For projects using Conan.

**conanfile.txt**:
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

**Install and use**:
```bash
conan install . --output-folder=build --build=missing
```

**CMakeLists.txt**:
```cmake
find_package(ThreadSchedule REQUIRED)

add_executable(your_app src/main.cpp)
target_link_libraries(your_app PRIVATE ThreadSchedule::ThreadSchedule)
```

## Verification

After installation, verify it works:

```cpp
#include <threadschedule/threadschedule.hpp>
#include <iostream>

int main() {
    using namespace threadschedule;
    
    HighPerformancePool pool(2);
    auto future = pool.submit([]() { return 42; });
    
    std::cout << "Result: " << future.get() << std::endl;
    return 0;
}
```

Build and run:
```bash
mkdir build && cd build
cmake ..
cmake --build .
./your_app
```

## Next Steps

- Read the [Quick Start Guide](quick-start.md) for basic usage examples
- Explore the [Integration Guide](../INTEGRATION.md) for advanced configuration options
- Check the [API Reference](../api/ThreadSchedule/index.md) for detailed documentation
