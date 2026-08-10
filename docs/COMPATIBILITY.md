# Compatibility and ABI

## Language contract

ThreadSchedule exposes one C++17 core API. It is compiled in CI as a consumer
under C++17, C++20, C++23, and C++26 where the selected compiler supports those
modes. The sole language-dependent addition is `threadschedule::jthread`,
which is declared when `std::jthread` support is detected. It intentionally has
no C++17 fallback type.

Public callable storage and `threadschedule::expected` keep one library-owned
C++17 representation in every mode. Apart from the separately named `jthread`,
feature-gated implementation code is only allowed when it cannot affect public
layout, symbols, or ODR-relevant inline definitions.

## Supported toolchain lines

| Platform | Tested line |
| --- | --- |
| Linux x86_64 | GCC 11, 13, 14, 15, and 16 with their matching libstdc++ |
| Linux ARM64 | GCC 13 and 14 with their matching libstdc++ |
| Windows | MSVC v143 on the recorded GitHub Actions Windows images |
| Windows | MinGW-w64/GCC from the recorded MSYS2 MINGW64 environment |

CI prints the full compiler and standard-library versions for every job. A new
compiler release becomes supported only after it is added to and passes this
matrix. Clang, macOS, and other environments are currently best effort.

Only the lowercase API documented in [API.md](API.md) and the names documented
in [ADVANCED.md](ADVANCED.md) form the supported v3 API. PascalCase backing
types in implementation headers are not compatibility entry points.

## Header-only and DSOs

Header-only mode does not create a compiler-neutral binary boundary. Passing
ThreadSchedule objects between DSOs requires identical v3 headers and a
compatible compiler, standard library, architecture, compile configuration,
and runtime-library mode.

The optional `ThreadSchedule::Runtime` centralizes the global registry, but it
uses the same C++ ABI contract. Do not mix GCC/libstdc++, MinGW, and MSVC
artifacts. ThreadSchedule 3.0 does not provide a C ABI or a portable plugin ABI.
