# Building from Source

This guide covers building ThreadSchedule from source code for development or custom deployment.

## Prerequisites

- **C++ Compiler**: GCC 9+, Clang 10+, MSVC 2019+, or MinGW-w64
- **CMake**: Version 3.14 or newer
- **Git**: For cloning the repository

## Quick Build

```bash
# Clone repository
git clone https://github.com/Katze719/ThreadSchedule.git
cd ThreadSchedule

# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
cmake --build .

# Run tests (optional)
ctest --output-on-failure
```

## Build Configurations

### Release Build

Optimized for production use:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### Debug Build

With debug symbols and assertions:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug
```

### RelWithDebInfo Build

Optimized with debug info:

```bash
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build . --config RelWithDebInfo
```

## Build Options

### Common Options

```bash
# Build examples
cmake .. -DTHREADSCHEDULE_BUILD_EXAMPLES=ON

# Build tests
cmake .. -DTHREADSCHEDULE_BUILD_TESTS=ON

# Build benchmarks
cmake .. -DTHREADSCHEDULE_BUILD_BENCHMARKS=ON

# Build shared runtime
cmake .. -DTHREADSCHEDULE_RUNTIME=ON

# Enable installation targets
cmake .. -DTHREADSCHEDULE_INSTALL=ON
```

### Combined Configuration

```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DTHREADSCHEDULE_BUILD_EXAMPLES=ON \
    -DTHREADSCHEDULE_BUILD_TESTS=ON \
    -DTHREADSCHEDULE_BUILD_BENCHMARKS=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local
```

## C++ Standards

ThreadSchedule supports C++17, C++20, and C++23:

```bash
# C++17 (default)
cmake .. -DCMAKE_CXX_STANDARD=17

# C++20
cmake .. -DCMAKE_CXX_STANDARD=20

# C++23
cmake .. -DCMAKE_CXX_STANDARD=23
```

## Platform-Specific Builds

### Linux

#### GCC

```bash
cmake .. -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

#### Clang

```bash
cmake .. -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

#### With Specific Compiler Version

```bash
cmake .. -DCMAKE_CXX_COMPILER=g++-11 -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### Windows

#### MSVC

```bash
# Configure for Visual Studio 2022
cmake .. -G "Visual Studio 17 2022" -A x64

# Build
cmake --build . --config Release
```

#### MinGW

```bash
# Configure for MinGW
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . -j%NUMBER_OF_PROCESSORS%
```

### macOS

```bash
# Using Clang (default)
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)
```

## Cross-Compilation

### Linux to Windows (MinGW)

**Toolchain file (mingw-toolchain.cmake)**:
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

**Build**:
```bash
mkdir build-mingw && cd build-mingw
cmake .. -DCMAKE_TOOLCHAIN_FILE=../mingw-toolchain.cmake
cmake --build .
```

### ARM64

```bash
# Native ARM64 build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Cross-compile from x86_64
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=arm64-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## Running Tests

### All Tests

```bash
cd build
ctest --output-on-failure
```

### Specific Test Suite

```bash
# Run only thread wrapper tests
ctest -R thread_wrapper -V

# Run only pool tests
ctest -R pool -V
```

### With Coverage

```bash
# Configure with coverage
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage"

# Build and test
cmake --build .
ctest

# Generate coverage report
gcov *.gcda
```

## Running Benchmarks

### All Benchmarks

```bash
cd build
./benchmarks/threadpool_basic_benchmarks
```

### Specific Benchmark

```bash
# Run image processing benchmarks
./benchmarks/image_processing_benchmarks

# Run with specific arguments
./benchmarks/threadpool_basic_benchmarks --benchmark_filter=HighPerformancePool
```

### Export Results

```bash
# Export to JSON
./benchmarks/threadpool_basic_benchmarks --benchmark_format=json --benchmark_out=results.json

# Export to CSV
./benchmarks/threadpool_basic_benchmarks --benchmark_format=csv --benchmark_out=results.csv
```

## Installation

### System-Wide Installation

```bash
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build .
sudo cmake --install .
```

### User Installation

```bash
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build .
cmake --install .
```

### Custom Location

```bash
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/opt/threadschedule
cmake --build .
sudo cmake --install .
```

## Development Setup

### With clangd

```bash
# Generate compile_commands.json
cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Link to project root for clangd
ln -s build/compile_commands.json .
```

### With IDE Support

#### Visual Studio Code

Install C++ extension and configure `.vscode/settings.json`:

```json
{
    "cmake.sourceDirectory": "${workspaceFolder}",
    "cmake.buildDirectory": "${workspaceFolder}/build"
}
```

#### CLion

Open CMakeLists.txt as a project. CLion will automatically configure CMake.

#### Visual Studio

```bash
# Generate Visual Studio solution
cmake .. -G "Visual Studio 17 2022" -A x64
```

Open `ThreadSchedule.sln` in Visual Studio.

## Troubleshooting

### Missing Dependencies

If CMake can't find dependencies:

```bash
# For tests
cmake .. -DTHREADSCHEDULE_BUILD_TESTS=OFF

# For benchmarks
cmake .. -DTHREADSCHEDULE_BUILD_BENCHMARKS=OFF
```

### Compiler Errors

Ensure you have C++17 support:

```bash
# Check GCC version
g++ --version  # Should be 9+

# Check Clang version
clang++ --version  # Should be 10+

# Force C++ standard
cmake .. -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON
```

### Link Errors

On Linux, ensure pthread is available:

```bash
# Install development tools
sudo apt-get install build-essential

# For Ubuntu/Debian
sudo apt-get install libpthread-stubs0-dev
```

### Windows-Specific Issues

#### MinGW Path Issues

```bash
# Add MinGW to PATH
set PATH=C:\mingw-w64\bin;%PATH%

# Verify
where g++
```

#### MSVC Not Found

```bash
# Run from Visual Studio Developer Command Prompt
# or use CMake GUI to select compiler
```

## Clean Build

```bash
# Remove build directory
rm -rf build

# Or clean within build directory
cd build
cmake --build . --target clean
```

## Build Performance

### Parallel Build

```bash
# Linux/macOS
cmake --build . -j$(nproc)

# Windows (PowerShell)
cmake --build . -j $env:NUMBER_OF_PROCESSORS

# Or specify number of jobs
cmake --build . -j8
```

### Compiler Cache (ccache)

```bash
# Install ccache
sudo apt-get install ccache  # Ubuntu/Debian

# Configure CMake to use ccache
cmake .. -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

# Build as usual
cmake --build .
```

## Next Steps

- Review [Contributing Guide](contributing.md) for development workflow
- Check [Performance Guide](performance.md) for optimization tips
- See [CMake Reference](../CMAKE_REFERENCE.md) for detailed build options
