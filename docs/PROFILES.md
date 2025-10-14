## Thread Profiles

High-level presets to simplify configuring threads and pools without fiddling with low-level flags.

### Presets
- realtime: Highest priority (Linux: FIFO if permitted; Windows: OTHER fallback), for hard real-time workloads.
- low_latency: Round-robin policy with elevated priority for responsiveness.
- throughput: Batch scheduling for bulk processing.
- background: Lowest priority for background tasks.

### API
Header: `include/threadschedule/profiles.hpp`

```cpp
#include <threadschedule/threadschedule.hpp>
using namespace threadschedule;

// Single thread
ThreadWrapper t([]{ /* work */ });
apply_profile(t, profiles::low_latency());

// ThreadPool
ThreadPool pool(8);
apply_profile(pool, profiles::throughput());

// HighPerformancePool
HighPerformancePool hp(16);
apply_profile(hp, profiles::realtime());

// Registry-managed thread by Tid
auto tid = ThreadInfo::get_thread_id();
apply_profile(registry(), tid, profiles::background());
```

### Optional Affinity in Profiles
You can embed a `ThreadAffinity` into a profile to pin threads:

```cpp
ThreadProfile p = profiles::low_latency();
p.affinity = ThreadAffinity({0,1});
apply_profile(pool, p);
```

### Notes
- Setting RT policies/priorities may require elevated capabilities (Linux: `CAP_SYS_NICE`).
- On Windows, policies map to priority; RT policies fall back to normal priority control.

