# ThreadSchedule v3 API Design

This document defines the target public API shape for ThreadSchedule 3.0.0.
It is the decision record for the breaking cleanup branch and should be kept
in sync with implementation work.

## Goals

- Provide one clean, unified, easy-to-learn user-facing C++ API.
- Make the stable ABI the foundation for every cross-DSO boundary.
- Keep C++17 as the minimum supported language mode.
- Preserve advanced native control without making common use cases expose OS
  scheduling details.
- Remove 2.x compatibility paths that make the public surface larger or less
  predictable.

## Public API Model

ThreadSchedule 3.0.0 has two explicit layers:

1. **Stable ABI layer**: `threadschedule::abi::*` plus exported
   `threadschedule_abi_*` functions. This is the only supported ABI boundary.
   It uses opaque handles, POD config structs, fixed-width enums, borrowed
   string views, function pointers, and explicit status codes.
2. **C++ convenience layer**: header-only C++17 wrappers over ABI handles and
   source-level implementation types. These wrappers may use RAII,
   `std::string_view`, templates, `expected`, and local C++ callbacks, but they
   must not be documented as ABI-stable.

No exported runtime function may expose STL types, C++ exceptions, references
to implementation classes, `std::function`, `std::future`, or concrete C++
object layouts.

## Naming And Shape Rules

- Prefer nouns for owning handles and config objects:
  `registry`, `thread`, `thread_pool`, `scheduled_pool`, `task_handle`.
- Prefer verbs for operations:
  `create`, `destroy`, `submit`, `shutdown`, `configure`, `cancel`.
- Use the same operation names across threads, pools, registry-controlled
  threads, and scheduled pools wherever the behavior is equivalent.
- Avoid multiple public names for the same concept. Keep aliases only when they
  are documented as migration-only or materially improve readability.
- Prefer config structs for operations with more than two independent options.
- Every operation that can fail returns an explicit result/status. ABI functions
  return `abi::status` or a POD result containing `status`.
- Header-only C++ wrappers may throw only where the API is explicitly named as
  throwing. Non-throwing APIs should be the default.
- Use one enum naming convention for all new v3 APIs: lower snake-case
  enumerators such as `normal`, `drop_pending`, and `permission_denied`.
- Public data fields use lower snake-case. Existing mixed-case fields such as
  `stdId` and `componentTag` need either migration aliases or a documented
  rename to `std_id` and `component_tag`.
- The umbrella header and C++20 module must expose the same intentional public
  surface. Implementation details, `detail::*` helpers, storage aliases, and
  internal queue/deque types must not be exported accidentally.
- Public wrappers with similar names must have equivalent capability. If a
  wrapper cannot forward the full underlying surface, name it as a limited
  adapter rather than as a peer pool type.

## Stable ABI Coverage

The ABI layer must cover:

- registry lifecycle, current registry lookup, iteration, and registration
- thread handles and thread control: name, affinity, scheduling, priority
- pool lifecycle, submit/post, shutdown, drain/drop behavior, statistics
- scheduled pool lifecycle, delayed/periodic scheduling, cancellation
- error callbacks, task completion callbacks, registry callbacks

Required handle types:

- `registry_handle`
- `thread_handle`
- `pool_handle`
- `scheduled_pool_handle`
- `scheduled_task_handle`

Required ABI data conventions:

- strings use `abi::string_ref`
- callbacks are function pointers plus `void* user_data`
- time/duration values use fixed-width integer fields with documented units
- bitsets and masks use fixed-width integer arrays or explicit count + pointer
- ownership is explicit in function names and documentation

## Scheduling And Priority

Scheduling and priority must be redesigned around user intent first.

Common user-facing intents:

- `background`
- `normal`
- `interactive`
- `low_latency`
- `realtime`

Native advanced controls remain available but must be explicit:

- POSIX nice priority for regular scheduling
- POSIX realtime priority for FIFO/RR, normally `1..99`
- Windows thread priority mapping
- process priority / nice value
- scheduling policy selection

Do not rely on ambiguous generic `highest()` / `lowest()` names where the
policy context changes the meaning. Prefer policy-specific constructors or
factory functions such as:

- `schedule::background()`
- `schedule::normal()`
- `schedule::low_latency()`
- `schedule::realtime_fifo(priority)`
- `schedule::realtime_rr(priority)`
- `schedule::native_windows_priority(value)`
- `schedule::posix_nice(value)`

Invalid combinations should be hard to express in the C++ wrapper layer and
reported clearly in the ABI layer. Platform-specific unsupported operations
must return explicit status codes rather than silently degrading.

## Pool API Direction

The 3.0.0 pool API should present one main user model:

- create/configure a pool
- submit work
- optionally observe completion/error
- shutdown with drain or drop behavior

Specialized pool implementations may remain internally, but users should not
need to learn several unrelated APIs for common work submission. Advanced pool
tuning should live in configuration objects, not in parallel class families
unless the implementation strategy is the main user-visible choice.

Canonical user-facing pool names should be chosen once. Template/base forms
such as implementation-specific pool bases, error adapters, global adapters, and
scheduled adapters should either be advanced APIs or hidden behind the unified
wrapper surface.

The ABI layer must not expose futures. C++ wrappers may build local futures by
allocating caller-side shared state and submitting ABI callbacks.

## Sub-Agent Workflow

Use sub-agents for independent review and implementation slices when it improves
quality or speed:

- API audit: public names, aliases, duplicate concepts, confusing entry points
- ABI audit: exported signatures, POD layouts, handle ownership, mixed-standard
  safety
- scheduling audit: Linux/Windows mapping, privilege behavior, presets
- test/migration audit: regression matrix and documentation coverage

Sub-agent findings must be merged into this document or linked from it before
large code changes proceed. Final API decisions stay centralized here.
