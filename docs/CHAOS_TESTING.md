## Chaos Testing for Threads

Use chaos to validate stability under changing runtime conditions like CPU migrations and priority perturbations.

### API
Header: `include/threadschedule/chaos.hpp`

```cpp
#include <threadschedule/threadschedule.hpp>
using namespace threadschedule;

// Apply chaos to all threads with a specific component tag
ChaosConfig cfg;
cfg.interval = std::chrono::milliseconds(200);
cfg.priority_jitter = 2;      // +/-2 around baseline
cfg.shuffle_affinity = true;  // periodically reshuffle to different NUMA CPUs

ChaosController controller(cfg, [](const RegisteredThreadInfo& e) {
    return e.componentTag == "io";
});

// RAII: chaos runs until controller is destroyed
```

### Tips
- Limit chaos to a subset of threads via the predicate.
- Keep intervals reasonable to avoid excessive thrashing.
- Combine with logging/metrics to observe impact.

