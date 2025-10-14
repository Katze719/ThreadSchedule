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
 * @brief Runtime chaos settings.
 */
struct ChaosConfig
{
    std::chrono::milliseconds interval{250};
    int priority_jitter{0}; // +/- jitter applied around current priority
    bool shuffle_affinity{true};
};

// RAII controller that periodically perturbs affinity/priority of registered threads matching a predicate
/**
 * @brief RAII controller that periodically applies chaos operations.
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

            // Priority jitter around current policy
            if (config_.priority_jitter != 0)
            {
                std::uniform_int_distribution<int> dist(-config_.priority_jitter, config_.priority_jitter);
                registry().apply(pred, [&](RegisteredThreadInfo const& info) {
                    int delta = dist(rng);
                    // Use normal as baseline if we can't read current
                    ThreadPriority prio = ThreadPriority::normal();
                    (void)registry().set_priority(info.tid, ThreadPriority{prio.value() + delta});
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
