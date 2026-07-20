# Advanced APIs

`threadschedule::advanced` is public and semver-stable, but is kept out of the
main getting-started path because its choices require workload or platform
knowledge.

Include the complete advanced surface explicitly:

```cpp
#include <threadschedule/advanced.hpp>
```

## Pool backends

| Type | Use case |
| --- | --- |
| `work_stealing_pool` | Irregular CPU workloads and high submission rates |
| `polling_pool` | Latency-sensitive workloads that accept periodic wakeups |
| `lightweight_pool` | Fire-and-forget tasks with bounded inline storage |
| `inline_pool` | Deterministic execution and tests |
| `global_thread_pool` | Lazily created process-wide standard pool |
| `global_work_stealing_pool` | Lazily created process-wide work-stealing pool |

Advanced pools retain backend-specific tuning and statistics. Their task
submission APIs are lower-level and may include explicitly throwing operations;
the canonical `thread_pool` remains the recommended default.

## Native scheduling

`native_thread_priority`, `native_scheduling_policy`, and
`scheduler_parameters` expose POSIX and Windows scheduling details. Realtime
configuration may require `CAP_SYS_NICE`, root privileges, or an elevated
Windows process.

`threadschedule::thread` deliberately exposes portable scheduling intent
through `thread_config::scheduling`, `schedule::*`, `priority_level`, and the
direct `set_priority` / `set_nice` operations. Native values differ between
POSIX and Windows and should be used only where an advanced API explicitly
accepts them. `advanced::native_schedule::posix_nice()` applies a real
per-thread nice value on Linux and the documented safe Win32 mapping on
Windows. Exact native realtime priority remains separate.

Normal Windows priority configuration does not alter the process priority
class and does not select `THREAD_PRIORITY_TIME_CRITICAL`. Under MinGW-w64,
the implementation obtains the Win32 `HANDLE` with `pthread_gethandle()`; a
`pthread_t` is never reinterpreted as a handle or thread ID.

`composite_thread_registry` can merge independent header-only registries when
using the shared runtime is not appropriate.

## Optional utilities

`advanced.hpp` is also the supported entry point for the following optional
facilities:

- future combinators: `when_all`, `when_any`, and `when_all_settled`
- scoped backend work through `task_group<Pool>`
- native scheduling presets through `thread_profile`, `profiles::*`, and
  `apply_profile`
- hardware discovery through `cpu_topology`, `read_topology`, and the NUMA
  affinity helpers
- test-only scheduling perturbation through `chaos_controller`
- lower-level callback dispatch through `error_handler`,
  `error_handled_task`, and `future_with_error_handler`

These names live in `threadschedule::advanced`. Their backing implementation
types are not an additional canonical core API. `task_group` is intended for
advanced pool backends whose `submit` operation directly returns a
`std::future`.

Advanced APIs use the same C++17 public type policy as the core API. Feature
detection may optimize implementation details but must not change a public
layout or signature.
