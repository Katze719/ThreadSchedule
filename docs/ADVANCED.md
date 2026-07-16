# Advanced APIs

`threadschedule::advanced` is public and semver-stable, but is kept out of the
main getting-started path because its choices require workload or platform
knowledge.

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
through `thread_config::scheduling` and `schedule::*`; it has no direct setter
for an exact `native_thread_priority`. Native values differ between POSIX and
Windows and should be used only where an advanced API explicitly accepts them.
For a portable owning thread, use `schedule::background()`,
`schedule::normal()`, `schedule::interactive()`, or
`schedule::low_latency()` and handle a configuration failure.

`composite_thread_registry` can merge independent header-only registries when
using the shared runtime is not appropriate.

Advanced APIs use the same C++17 public type policy as the core API. Feature
detection may optimize implementation details but must not change a public
layout or signature.
