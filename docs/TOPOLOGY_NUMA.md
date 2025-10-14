## Topology and NUMA

NUMA (Non-Uniform Memory Access) describes systems where memory latency/bandwidth depends on which CPU socket (node) a thread runs on. Keeping threads on CPUs near their memory (NUMA-local) reduces latency and cache/memory traffic.

This library provides helpers to discover a basic topology and construct NUMA-aware CPU affinity masks.

### Concepts
- CPU: Logical processor index (0..N-1).
- NUMA node: Group of CPUs with local memory. Access to remote nodes has higher latency.
- Affinity: Bitmask of CPUs a thread is allowed to run on.

### API
Header: `include/threadschedule/topology.hpp`

```cpp
#include <threadschedule/threadschedule.hpp>
using namespace threadschedule;

CpuTopology topo = read_topology();
// topo.cpu_count, topo.numa_nodes, topo.node_to_cpus[node]

// Pin one thread to a specific NUMA node (round-robin CPU selection within node)
ThreadAffinity aff = affinity_for_node(/*node_index*/ 0, /*thread_index*/ 0);
ThreadWrapper t([]{ /* work */ });
(void)t.set_affinity(aff);

// Distribute a pool across NUMA nodes
ThreadPool pool(8);
auto affs = distribute_affinities_by_numa(pool.size());
for (size_t i = 0; i < pool.size(); ++i) {
    // In simple ThreadPool: use set_affinity returning bool
    (void)pool.set_affinity(affs[i]);
}
```

### When to use NUMA-aware affinity
- Memory-bound workloads with per-thread state/buffers
- Databases, caches, audio/video pipelines, networking stacks
- Latency-sensitive services that benefit from cache locality

### Notes
- Linux: Topology is read from `/sys/devices/system/node/node*/cpulist`.
- Windows: Fallback is a single-node view; group-affinity is handled inside thread setters.
- Combine with profiles for best results: e.g. low-latency + node-local affinity.

