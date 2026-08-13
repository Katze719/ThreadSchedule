#pragma once

/**
 * @file chaos_controller.hpp
 * @brief Test/chaos features to perturb scheduling at runtime.
 *
 * Provides `chaos_controller`, an RAII utility that periodically alters
 * affinity and/or priority of threads selected from the global registry
 * (by predicate). Useful to validate stability under core migrations,
 * contention, and priority changes.
 */

#include "../detail/scheduling/native.hpp"
#include "../detail/thread_backend.hpp"
#include "../thread_registry.hpp"
#include "cpu_topology.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <random>
#include <thread>

namespace threadschedule::advanced
{

namespace implementation = ::threadschedule::detail;
using native_scheduling_policy = implementation::native_scheduling_policy;
using native_thread_affinity = implementation::native_thread_affinity;
using native_thread_id = implementation::native_thread_id;
using native_thread_priority = implementation::native_thread_priority;

/**
 * @brief Plain value type holding runtime chaos-testing parameters.
 *
 * All fields have sensible defaults so a default-constructed `chaos_config`
 * is immediately usable.
 *
 * @see chaos_controller
 */
struct chaos_config
{
  /** Time between successive chaos perturbations (default 250 ms). */
  std::chrono::milliseconds interval{ 250 };

  /**
   * @brief +/- range applied around the current thread priority each
   *        interval.
   *
   * A value of 0 disables priority perturbation.
   */
  int priority_jitter{ 0 };

  /** Whether to reassign CPU affinities each interval (default `true`). */
  bool shuffle_affinity{ true };
};

/**
 * @brief RAII controller that periodically perturbs scheduling attributes
 *        of registered threads for chaos/fuzz testing.
 *
 * On construction, `chaos_controller` spawns a background control thread
 * that wakes every `chaos_config::interval` and applies perturbations
 * (affinity shuffling, priority jitter) to threads in the global
 * `global_registry()` that match the user-supplied predicate. The predicate
 * receives a portable `registered_thread` snapshot.
 *
 * **Ownership semantics:**
 * - Non-copyable, non-movable.
 * - The destructor signals the worker to stop and **blocks** until it
 *   joins. Do not destroy from a context where blocking is unacceptable.
 *
 * **Thread safety:**
 * The controller operates on `global_registry()`, which is internally
 * synchronized, so multiple controllers or concurrent registrations are
 * safe.
 *
 * @warning Intended for testing and validation only - not for production
 *          use. Perturbations may cause spurious priority inversions and
 *          cache-thrashing.
 *
 * @par Example
 * @code
 * chaos_config cfg;
 * cfg.interval = 100ms;
 * cfg.priority_jitter = 5;
 * chaos_controller chaos(cfg, [](auto const& info) {
 *     return info.name.starts_with("worker");
 * });
 * // ... run tests while chaos is active ...
 * // destructor joins the worker thread
 * @endcode
 *
 * @see chaos_config, global_registry()
 */
class chaos_controller
{
public:
  template <typename Predicate>
  chaos_controller(chaos_config cfg, Predicate pred) : config_(cfg), stop_(false)
  {
    std::promise<native_thread_id> worker_started;
    auto worker_ready = worker_started.get_future();

    worker_ = implementation::thread_backend(
        [this, pred, started = std::move(worker_started)]() mutable
          {
            started.set_value(implementation::thread_info::get_thread_id());
            run_loop(pred);
          });

    worker_tid_ = worker_ready.get();
    (void)implementation::thread_info(worker_tid_).set_name("ts_chaos_ctl");
  }

  ~chaos_controller()
  {
    stop_ = true;
    if (worker_.joinable())
      worker_.join();
  }

  chaos_controller(chaos_controller const&) = delete;
  auto operator=(chaos_controller const&) -> chaos_controller& = delete;

  [[nodiscard]] auto
  thread_info() const -> std::optional<registered_thread>
  {
    if (!worker_.joinable() || worker_tid_ == native_thread_id{})
      return std::nullopt;
    auto const name = implementation::thread_info(worker_tid_).get_name();
    return registered_thread{ static_cast<std::uint64_t>(worker_tid_), worker_.get_id(), name.value_or(std::string{}),
                              "chaos", true };
  }

  auto
  configure_thread(std::string const& name, native_scheduling_policy policy = native_scheduling_policy::other,
                   native_thread_priority priority = native_thread_priority::normal())
      -> expected<void, std::error_code>
  {
    if (!worker_.joinable() || worker_tid_ == native_thread_id{})
      return unexpected(std::make_error_code(std::errc::no_such_process));
    auto info = implementation::thread_info(worker_tid_);
    return implementation::configure_thread(info, name, policy, priority);
  }

private:
  template <typename Predicate>
  void
  run_loop(Predicate pred)
  {
    std::mt19937 rng(std::random_device{}());
    while (!stop_)
      {
        auto selected = [&pred](implementation::registered_thread_info_backend const& info)
          {
            return std::invoke(pred, registered_thread{ static_cast<std::uint64_t>(info.tid), info.std_id, info.name,
                                                        info.component, info.alive });
          };
        implementation::runtime_registry().apply(selected,
                                                 [&](implementation::registered_thread_info_backend const& info)
                                                   {
                                                     auto blk = implementation::runtime_registry().get(info.tid);
                                                     (void)blk;
                                                   });

        // Affinity shuffle using topology
        if (config_.shuffle_affinity)
          {
            auto topo = read_topology();
            size_t idx = 0;
            implementation::runtime_registry().apply(
                selected,
                [&](implementation::registered_thread_info_backend const& info)
                  {
                    native_thread_affinity aff = affinity_for_node(
                        static_cast<int>(idx % (topo.numa_nodes > 0 ? topo.numa_nodes : 1)), static_cast<int>(idx));
                    (void)implementation::runtime_registry().set_affinity(info.tid, aff);
                    ++idx;
                  });
          }

        // Priority jitter around the thread's actual priority
        if (config_.priority_jitter != 0)
          {
            std::uniform_int_distribution<int> dist(-config_.priority_jitter, config_.priority_jitter);
            implementation::runtime_registry().apply(selected,
                                                     [&](implementation::registered_thread_info_backend const& info)
                                                       {
                                                         int delta = dist(rng);
                                                         int baseline = native_thread_priority::normal().value();
#ifndef _WIN32
                                                         sched_param sp{};
                                                         if (sched_getparam(info.tid, &sp) == 0)
                                                           baseline = sp.sched_priority;
#endif
                                                         (void)implementation::runtime_registry().set_priority(
                                                             info.tid, native_thread_priority{ baseline + delta });
                                                       });
          }

        std::this_thread::sleep_for(config_.interval);
      }
  }

  chaos_config config_;
  std::atomic<bool> stop_;
  implementation::thread_backend worker_;
  native_thread_id worker_tid_{};
};

} // namespace threadschedule::advanced
