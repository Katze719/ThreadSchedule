# Registry integration tests

These projects validate the supported C++17 registry integration models across
real shared libraries.

## `runtime_single`

The application and two DSOs link `ThreadSchedule::Runtime` and observe one
process-wide registry. All components must use the same supported compiler,
standard-library ABI, architecture, runtime-library mode, and v3 headers.

## `app_injection`

An application-owned `thread_registry` is installed with
`use_global_registry()` before worker threads start. This is appropriate when
the application controls every linked image and their C++ ABI is compatible.

## `composite_merge`

Each library owns a registry and the application combines their snapshots with
`advanced::composite_thread_registry`. Ownership remains with the libraries.

Each directory is a standalone CMake project. Install ThreadSchedule to a test
prefix, configure the chosen project with `CMAKE_PREFIX_PATH`, then build and
run CTest.
