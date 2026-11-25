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
in [ADVANCED.md](ADVANCED.md) form the supported API. PascalCase backing
types in implementation headers are not compatibility entry points.

## Header-only and DSOs

Header-only mode does not create a compiler-neutral binary boundary. Passing
ThreadSchedule objects between DSOs requires identical ThreadSchedule headers and a
compatible compiler, standard library, architecture, compile configuration,
and runtime-library mode.

The optional `ThreadSchedule::Runtime` centralizes the global registry, but it
uses the same C++ ABI contract. Do not mix GCC/libstdc++, MinGW, and MSVC
artifacts. ThreadSchedule does not provide a C ABI or a portable plugin ABI.

### `expected` across GCC versions

`threadschedule::expected<T, E>` uses the same storage design across C++
language modes. Its binary layout also depends on the compiler, standard
library, and the layouts of `T` and `E`.

On Linux x86-64, a shared library compiled with GCC 13.3 and C++17
successfully returned `result<int>`, `result<void>`,
`result<std::string>`, and `expected<void, std::string>` to a GCC 16.2
consumer compiled as C++26. Both sides used identical ThreadSchedule headers.
The following payload exposed a layout difference in the same test.

```cpp
#include <threadschedule/expected.hpp>

struct empty
{
};
struct payload
{
  [[no_unique_address]] const empty first{};
  [[no_unique_address]] empty second{};
  char value{};
};
using result = threadschedule::expected<payload, unsigned char>;
```

`sizeof(result)` was 2 with GCC 13.3 and 3 with GCC 16.2, including when the
GCC 16.2 consumer also used C++17. GCC 14 changed the layout of cv-qualified
`[[no_unique_address]]` members. [GCC C++ ABI version history](https://gcc.gnu.org/onlinedocs/gcc-16.1.0/gcc/C_002b_002b-Dialect-Options.html#index-fabi-version)

Use the same libstdc++ ABI settings on both sides, including
`_GLIBCXX_USE_CXX11_ABI`. If you support mixed GCC versions, test each `expected`
type exposed across the library boundary with the compiler versions and build
flags you ship. [libstdc++ dual ABI](https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_dual_abi.html)
