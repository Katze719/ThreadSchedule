#pragma once

#include "../../thread.hpp"
#include "../../thread_config.hpp"
#include "../../thread_registry.hpp"
#include "../cpu_topology.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <random>
#include <thread>
#include <utility>

namespace threadschedule::advanced
{

struct chaos_config
{
  std::chrono::milliseconds interval{ 250 };
  int nice_jitter{ 0 };
  bool shuffle_affinity{ true };
};

class chaos_controller
{
public:
  template <typename Predicate>
  chaos_controller(chaos_config config, Predicate predicate) : config_(config)
  {
    if (config_.interval <= std::chrono::milliseconds::zero())
      throw std::invalid_argument("chaos interval must be positive");
    if (config_.nice_jitter < 0)
      throw std::invalid_argument("chaos nice jitter must not be negative");

    std::promise<thread_id> started;
    auto ready = started.get_future();
    worker_ = thread(
        [this, predicate = std::move(predicate), started = std::move(started)]() mutable
          {
            started.set_value(
                detail::thread_id_access::make(static_cast<std::uint64_t>(detail::current_native_thread_id())));
            run_loop(predicate);
          });
    worker_id_ = ready.get();
    (void)worker_.set_name("ts_chaos_ctl");
  }

  ~chaos_controller()
  {
    stop_.store(true, std::memory_order_release);
    if (worker_.joinable())
      (void)worker_.join();
  }

  chaos_controller(chaos_controller const&) = delete;
  auto operator=(chaos_controller const&) -> chaos_controller& = delete;
  chaos_controller(chaos_controller&&) = delete;
  auto operator=(chaos_controller&&) -> chaos_controller& = delete;

  [[nodiscard]] auto
  thread_info() const -> std::optional<registered_thread>
  {
    if (!worker_.joinable() || !worker_id_)
      return std::nullopt;
    auto name = worker_.get_name();
    return registered_thread{ *worker_id_, worker_.get_id(), name.value_or(std::string{}), "chaos", true };
  }

  auto
  configure_thread(thread_config const& config) -> result<void>
  {
    if (!worker_.joinable())
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return worker_.configure(config);
  }

private:
  template <typename Predicate>
  void
  run_loop(Predicate& predicate)
  {
    std::mt19937 random(std::random_device{}());
    while (!stop_.load(std::memory_order_acquire))
      {
        auto entries = global_registry().snapshot();
        if (entries)
          perturb(*entries, predicate, random);
        std::this_thread::sleep_for(config_.interval);
      }
  }

  template <typename Predicate>
  void
  perturb(std::vector<registered_thread> const& entries, Predicate& predicate, std::mt19937& random)
  {
    auto const topology = config_.shuffle_affinity ? read_topology() : cpu_topology{};
    std::uniform_int_distribution<int> jitter(-config_.nice_jitter, config_.nice_jitter);
    std::size_t selected_index = 0;
    for (auto const& entry : entries)
      {
        if (!entry.alive || !std::invoke(predicate, entry))
          continue;

        if (config_.shuffle_affinity)
          {
            auto const node = topology.numa_nodes == 0 ? 0 : selected_index % topology.numa_nodes;
            thread_config affinity;
            affinity.set_affinity(
                affinity_for_node(topology, static_cast<int>(node), static_cast<int>(selected_index)));
            (void)global_registry().configure(entry.id, affinity);
          }

        if (config_.nice_jitter != 0)
          {
            auto current = global_registry().get_nice(entry.id);
            if (current)
              {
                auto const value
                    = std::clamp(current->value() + jitter(random), nice_value::minimum, nice_value::maximum);
                (void)global_registry().set_nice(entry.id, nice_value{ value });
              }
          }
        ++selected_index;
      }
  }

  chaos_config config_;
  std::atomic<bool> stop_{ false };
  thread worker_;
  std::optional<thread_id> worker_id_;
};

} // namespace threadschedule::advanced
