#pragma once

/**
 * @file chaos.hpp
 * @brief Test/chaos features to perturb scheduling at runtime.
 *
 * Provides `ChaosController`, an RAII utility that periodically alters
 * affinity and/or priority of threads selected from the global registry
 * (by predicate). Useful to validate stability under core migrations,
 * contention, and priority changes.
 */

#include "scheduler_policy.hpp"
#include "thread_registry.hpp"
#include "thread_wrapper.hpp"
#include "topology.hpp"
#include <atomic>
#include <chrono>
#include <random>
#include <thread>

namespace threadschedule
{

/**
 * @brief Plain value type holding runtime chaos-testing parameters.
 *
 * All fields have sensible defaults so a default-constructed `ChaosConfig`
 * is immediately usable.
 *
 * @see ChaosController
 */
struct ChaosConfig
{
    /** Time between successive chaos perturbations (default 250 ms). */
    std::chrono::milliseconds interval{250};

    /**
     * @brief +/- range applied around the current thread priority each
     *        interval.
     *
     * A value of 0 disables priority perturbation.
     */
    int priority_jitter{0};

    /** Whether to reassign CPU affinities each interval (default `true`). */
    bool shuffle_affinity{true};
};

/**
 * @brief RAII controller that periodically perturbs scheduling attributes
 *        of registered threads for chaos/fuzz testing.
 *
 * On construction, `ChaosController` spawns a background `std::thread`
 * that wakes every `ChaosConfig::interval` and applies perturbations
 * (affinity shuffling, priority jitter) to threads in the global
 * `registry()` that match the user-supplied predicate.
 *
 * **Ownership semantics:**
 * - Non-copyable, non-movable.
 * - The destructor signals the worker to stop and **blocks** until it
 *   joins. Do not destroy from a context where blocking is unacceptable.
 *
 * **Thread safety:**
 * The controller operates on the global `registry()`, which is internally
 * synchronized, so multiple controllers or concurrent registrations are
 * safe.
 *
 * @warning Intended for testing and validation only - not for production
 *          use. Perturbations may cause spurious priority inversions and
 *          cache-thrashing.
 *
 * @par Example
 * @code
 * ChaosConfig cfg{.interval = 100ms, .priority_jitter = 5};
 * ChaosController chaos(cfg, [](auto const& info) {
 *     return info.name.starts_with("worker");
 * });
 * // ... run tests while chaos is active ...
 * // destructor joins the worker thread
 * @endcode
 *
 * @see ChaosConfig, registry()
 */
class ChaosController
{
  public:
    template <typename Predicate>
    ChaosController(ChaosConfig cfg, Predicate pred)
        : config_(cfg), stop_(false), worker_([this, pred]() { run_loop(pred); })
    {
    }

    ~ChaosController()
    {
        stop_ = true;
        if (worker_.joinable())
            worker_.join();
    }

    ChaosController(ChaosController const&) = delete;
    auto operator=(ChaosController const&) -> ChaosController& = delete;

  private:
    template <typename Predicate>
    void run_loop(Predicate pred)
    {
        std::mt19937 rng(std::random_device{}());
        while (!stop_)
        {
            registry().apply(pred, [&](RegisteredThreadInfo const& info) {
                auto blk = registry().get(info.tid);
                (void)blk;
            });

            // Affinity shuffle using topology
            if (config_.shuffle_affinity)
            {
                auto topo = read_topology();
                size_t idx = 0;
                registry().apply(pred, [&](RegisteredThreadInfo const& info) {
                    ThreadAffinity aff = affinity_for_node(
                        static_cast<int>(idx % (topo.numa_nodes > 0 ? topo.numa_nodes : 1)), static_cast<int>(idx));
                    (void)registry().set_affinity(info.tid, aff);
                    ++idx;
                });
            }

            // Priority jitter around the thread's actual priority
            if (config_.priority_jitter != 0)
            {
                std::uniform_int_distribution<int> dist(-config_.priority_jitter, config_.priority_jitter);
                registry().apply(pred, [&](RegisteredThreadInfo const& info) {
                    int delta = dist(rng);
                    int baseline = ThreadPriority::normal().value();
#ifndef _WIN32
                    sched_param sp{};
                    if (sched_getparam(info.tid, &sp) == 0)
                        baseline = sp.sched_priority;
#endif
                    (void)registry().set_priority(info.tid, ThreadPriority{baseline + delta});
                });
            }

            std::this_thread::sleep_for(config_.interval);
        }
    }

    ChaosConfig config_;
    std::atomic<bool> stop_;
    std::thread worker_;
};

} // namespace threadschedule
