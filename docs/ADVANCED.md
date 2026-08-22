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
| `raw_thread_pool` | Direct access to the core shared-queue implementation |
| `work_stealing_pool` | Irregular CPU workloads and high submission rates |
| `polling_pool` | Latency-sensitive workloads that accept periodic wakeups |
| `lightweight_pool` | Fire-and-forget tasks with bounded inline storage |
| `inline_pool` | Deterministic execution and tests |
| `global_thread_pool` | Lazily created process-wide standard pool |
| `global_work_stealing_pool` | Lazily created process-wide work-stealing pool |
| `raw_scheduled_pool` | Scheduler backed by `raw_thread_pool` |
| `scheduled_work_stealing_pool` | Scheduler backed by work stealing |
| `scheduled_polling_pool` | Scheduler backed by polling workers |
| `scheduled_lightweight_pool` | Scheduler backed by lightweight workers |

Advanced pools are facades over specialized backends; no backend type or
constructor leaks into their public API. They use `worker_count`,
`worker_registration`, `thread_config`, `shutdown_policy`, and `result<T>` just
like the core pools. Throwing operations are explicitly named
`submit_or_throw`, `post_or_throw`, and `submit_batch_or_throw`. Common
statistics use the public `pool_statistics` value type. The canonical
`thread_pool` remains the recommended default.

The asynchronous specialized pools provide `shutdown_for(milliseconds)` for a timed drain. Non-positive timeouts expire
immediately. The deadline also covers waiting for another concurrent shutdown operation to release lifecycle ownership.
Positive timeouts that exceed the representable `steady_clock` deadline saturate at
`steady_clock::time_point::max()` instead of overflowing. Once a finite timeout expires, queued work is discarded without
joining an already-running C++ callable; that callable finishes asynchronously. A later blocking `shutdown()` or pool
destruction joins the remaining workers.

The lightweight pool's default callable slot is exactly 64 bytes, including its dispatch pointer. On 64-bit platforms this
leaves 56 bytes of inline storage; larger or over-aligned callables use the heap fallback.

## Native scheduling

`advanced::native_handle(thread)` and its C++20 `jthread` overload provide the
platform-native handle escape hatch. The portable core types intentionally do
not expose toolchain-specific handle types as member functions.

Scheduling intent remains portable even in advanced profiles: use
`thread_config`, `schedule::*`, `priority_level`, `nice_value`, and
`realtime_priority`. ThreadSchedule does not publish a second native scheduling
type system. Code that truly needs a POSIX or Win32-only policy uses
`advanced::native_handle(...)` and calls the platform API directly. Raw OS
thread IDs exist only as `advanced::native_thread_id`, for APIs such as Linux
cgroup attachment. Converting a registry `thread_id` with
`advanced::native_id(...)` returns a `result<native_thread_id>` and rejects
values that cannot fit the platform ID type.

Normal Windows priority configuration does not alter the process priority
class and does not select `THREAD_PRIORITY_TIME_CRITICAL`. Under MinGW-w64,
the implementation obtains the Win32 `HANDLE` with `pthread_gethandle()`; a
`pthread_t` is never reinterpreted as a handle or thread ID.

Windows `cpu_id` values use `processor_group * 64 + processor_index`.
`thread_affinity` represents one processor group at a time, matching
`SetThreadGroupAffinity`. On Windows versions where a default thread may run
in more than one group, `get_affinity()` reports the primary group exposed by
`GetThreadGroupAffinity`; it does not claim to represent the all-group default.
Pool distribution therefore stays within the caller's reported group unless
workers are configured explicitly with per-group masks.

## Linux lookup by thread name

`advanced::thread_by_name_view` finds unregistered threads in the current
process by an exact `/proc/self/task/*/comm` match. Include
`<threadschedule/advanced/thread_by_name_view.hpp>`. Linux names must be
non-empty and at most 15 bytes.

```cpp
auto worker = advanced::thread_by_name_view::create("io-worker");
if (!worker)
  report(worker.error());
else if (auto pinned = worker->set_affinity(allowed_worker_cpus); !pinned)
  report(pinned.error());
```

`create()` requires exactly one match: no match returns `no_such_process` and
multiple matches return `invalid_argument`. `find_all()` returns every match
ordered by native thread ID, including an empty vector when there is no match.
Direct construction provides the same singular lookup and throws
`std::system_error` on failure.

A view stays bound to the original native ID and Linux start-time generation
after a rename. `alive()` and every control operation verify both kernel TID
existence and that generation, rejecting an exited or recycled target. Linux
nevertheless provides no handle-based form of the
supported TID control syscalls, so an unregistered thread can exit between the
last identity check and the syscall. Use `thread_registry` for lifecycle-bound
control. Lookup returns `function_not_supported` on Windows.

`composite_thread_registry` can merge independent header-only registries when
using the shared runtime is not appropriate.

Advanced facilities also have self-contained focused headers. Common choices
are `<threadschedule/advanced/pools.hpp>`,
`<threadschedule/advanced/native_thread.hpp>`,
`<threadschedule/advanced/thread_by_name_view.hpp>`,
`<threadschedule/advanced/thread_profile.hpp>`, and
`<threadschedule/advanced/composite_thread_registry.hpp>`. The full
`advanced.hpp` umbrella is provided for applications that intentionally use
several optional facilities.

Focused headers that primarily define a class use that class name, including
`cpu_topology.hpp`; chaos support intentionally lives at
`advanced/testing/chaos_controller.hpp`. Function-only collections such
as `futures.hpp` retain a responsibility-based name.

## Optional utilities

`advanced.hpp` is also the supported entry point for the following optional
facilities:

- future combinators: `when_all`, `when_any`, and `when_all_settled`
- scoped backend work through `task_group<Pool>`
- portable scheduling presets through `thread_profile`, `profiles::*`, and
  `apply_profile`
- hardware discovery through `cpu_topology`, `read_topology`, and the NUMA
  affinity helpers
- test-only scheduling perturbation through `chaos_controller`
- lower-level callback dispatch through `error_handler`,
  `error_handled_task`, and `future_with_error_handler`

These names live in `threadschedule::advanced`. Their backing implementation
types are not an additional canonical core API. `task_group` is intended for
advanced pool facades. Their `submit` operation follows the common
`result<std::future<T>>` contract.

`task_group::wait()` also waits for child work submitted to the same group by
an already tracked task. Batch submission accepts single-pass input iterators;
`parallel_for_each` requires at least forward iterators because worker chunks
retain iterator pairs until execution completes.

On Linux, `read_topology()` reads the sysfs `has_cpu` node list rather than
assuming contiguous NUMA identifiers. The returned mapping is compact and
contains CPU-bearing nodes only, so NUMA distribution never intentionally
selects a memory-only node. Attaching a moved-from registry to
`composite_thread_registry` throws `std::system_error` with
`operation_canceled`.

Advanced APIs use the same C++17 public type policy as the core API. Feature
detection may optimize implementation details but must not change a public
layout or signature.
