## Unreleased

## v1.2.0

- Added: Windows thread affinity retrieval via `GetThreadGroupAffinity` in `include/threadschedule/thread_wrapper.hpp`
- Added: Integration test `integration_tests/runtime_abi_compat` to validate ABI compatibility (shared runtime) between current library and older tags
- Added: Parameterization for ABI test old version selection via `RUNTIME_ABI_OLD_REF` or `RUNTIME_ABI_OLD_OFFSET`
- Added: GitHub Actions workflow `abi-compat.yml` to run ABI tests on Linux and Windows for the last 3 tags; allowed failure only on major version bumps (or when explicitly enabled)
- Docs: Updated `integration_tests/README.md` with usage for ABI compatibility scenario

## v1.1.0

- Improve thread profile application (`apply_profile`)

## v1.0.0

- Refactor `ThreadControlBlock` and `RegisteredThreadInfo`

## v1.0.0-rc.5

- Add thread profiles, NUMA helpers, and chaos testing documentation
- Refactor `expected` class and error handling
- Update Doxygen to 1.14 and fix warnings

## v1.0.0-rc.4

- Add Doxygen documentation and theme integration
- Improve registry and control callbacks, ensure thread-safety
- Set name on control block creation

## v1.0.0-rc.3

- Documentation: registry diagrams and ownership clarifications
- Thread wrappers: ownership transfer methods and tests
- Benchmarks and documentation improvements

## v1.0.0-rc.2

- Chainable query API for thread registry
- Documentation and examples: scheduled tasks, error handling
- Roadmap and status in README
- Testing refactors and improvements

## v1.0.0-rc.1

- Integration testing framework and post-build steps
- App injection and composite merge libraries with registry support
- CI/documentation refinements

## v1.0.0-alpha.1

- Windows runtime post-build steps for integration tests
- Dynamic linking support on Windows for libraries
- Enforce shared runtime for MSVC
- CI improvements: ARM64, workflows, and documentation

## v0.4.0

- Global control registry and registry guide
- Non-owning thread views (`ThreadWrapperView`, `JThreadWrapperView`)
- CMake and CI modernization; expected type and tests

## v0.3.1

- CI refactor: split workflows, badges update, cleanup
- Fix CI meta

## v0.3.0

- Windows support for thread wrappers

## v0.2.0

- Benchmarks and resampling benchmark additions
- CMake refactor and integration guide

## v0.1.0

- Initial benchmark suite and examples


