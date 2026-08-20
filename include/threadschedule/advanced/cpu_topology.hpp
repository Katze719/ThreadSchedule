#pragma once

/**
 * @file cpu_topology.hpp
 * @brief Hardware topology helpers (CPU count, NUMA nodes) and affinity
 * builders.
 *
 * Exposes lightweight discovery of CPU/NUMA topology and convenience
 * functions to construct NUMA-aware `thread_affinity` masks. On Linux,
 * NUMA nodes are detected via sysfs (nodeX/cpulist). On Windows, processor
 * groups are enumerated and represented as flat @c group*64+index IDs.
 */

#include "../thread_affinity.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <thread>
#include <vector>

#ifndef _WIN32
#  include <fstream>
#  include <string>
#endif

#ifdef _WIN32
#  include "../detail/windows_api.hpp"
#endif

namespace threadschedule::advanced
{

#ifndef _WIN32
namespace topology_detail
{
[[nodiscard]] inline auto
parse_index_list(std::string_view input) -> std::vector<int>
{
  std::vector<int> values;
  auto const* cursor = input.data();
  auto const* const end = cursor + input.size();

  while (cursor != end)
    {
      while (cursor != end && (*cursor == ',' || *cursor == ' ' || *cursor == '\t' || *cursor == '\n'))
        ++cursor;
      if (cursor == end)
        break;

      int first = 0;
      auto parsed = std::from_chars(cursor, end, first);
      if (parsed.ec != std::errc{} || parsed.ptr == cursor || first < 0)
        return {};
      cursor = parsed.ptr;

      int last = first;
      if (cursor != end && *cursor == '-')
        {
          ++cursor;
          parsed = std::from_chars(cursor, end, last);
          if (parsed.ec != std::errc{} || parsed.ptr == cursor || last < first)
            return {};
          cursor = parsed.ptr;
        }

      for (int value = first;; ++value)
        {
          values.push_back(value);
          if (value == last)
            break;
        }

      if (cursor != end && *cursor != ',' && *cursor != ' ' && *cursor != '\t' && *cursor != '\n')
        return {};
    }

  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

[[nodiscard]] inline auto
read_index_list(std::string const& path) -> std::vector<int>
{
  std::ifstream input(path);
  std::string contents;
  if (!std::getline(input, contents))
    return {};
  return parse_index_list(contents);
}
} // namespace topology_detail
#endif

/**
 * @brief Snapshot of basic CPU/NUMA topology.
 *
 * Value type (copyable). Populated by read_topology().
 *
 * - @c cpu_count: total logical CPUs (from @c
 * std::thread::hardware_concurrency).
 * - @c numa_nodes: number of CPU-bearing NUMA nodes (always 1 on Windows;
 *   detected via the Linux sysfs node lists).
 * - @c node_to_cpus: mapping from NUMA node index to the set of
 *   logical CPU indices belonging to that node.
 */
struct cpu_topology
{
  std::size_t cpu_count{ 0 };
  std::size_t numa_nodes{ 1 };
  std::vector<std::vector<cpu_id>> node_to_cpus;
};

/**
 * @brief Discover basic topology. Linux: reads /sys for NUMA nodes.
 *        Windows: single node, processor-group-aware CPU indices.
 *
 * Called frequently by chaos/affinity helpers. The result is not
 * cached internally - consider caching the returned cpu_topology
 * yourself if performance of repeated calls matters.
 */
inline auto
read_topology() -> cpu_topology
{
  cpu_topology topo;
  topo.cpu_count = std::thread::hardware_concurrency();
  if (topo.cpu_count == 0)
    topo.cpu_count = 1;

#ifdef _WIN32
  topo.numa_nodes = 1;
  topo.node_to_cpus = { {} };
  topo.cpu_count = 0;

#  ifndef THREADSCHEDULE_WINDOWS_VISTA_COMPAT
  using get_active_processor_group_count_fn = WORD(WINAPI*)();
  using get_active_processor_count_fn = DWORD(WINAPI*)(WORD);

  HMODULE const kernel32 = GetModuleHandleW(L"kernel32.dll");
  if (kernel32)
    {
      auto const get_group_count = reinterpret_cast<get_active_processor_group_count_fn>(
          reinterpret_cast<void*>(GetProcAddress(kernel32, "GetActiveProcessorGroupCount")));
      auto const get_processor_count = reinterpret_cast<get_active_processor_count_fn>(
          reinterpret_cast<void*>(GetProcAddress(kernel32, "GetActiveProcessorCount")));
      if (get_group_count && get_processor_count)
        {
          WORD const group_count = get_group_count();
          for (WORD group = 0; group < group_count; ++group)
            {
              DWORD const processor_count = get_processor_count(group);
              if (processor_count == 0)
                continue;
              topo.cpu_count += static_cast<std::size_t>(processor_count);
              for (DWORD index = 0; index < processor_count; ++index)
                topo.node_to_cpus[0].emplace_back(static_cast<int>(group) * 64 + static_cast<int>(index));
            }
        }
    }
#  endif

  if (topo.node_to_cpus[0].empty())
    {
      SYSTEM_INFO system_info{};
      GetSystemInfo(&system_info);
      DWORD const processor_count = system_info.dwNumberOfProcessors > 0 ? system_info.dwNumberOfProcessors : 1;
      topo.cpu_count = static_cast<std::size_t>(processor_count);
      for (DWORD index = 0; index < processor_count; ++index)
        topo.node_to_cpus[0].emplace_back(static_cast<int>(index));
    }
#else
  auto node_ids = topology_detail::read_index_list("/sys/devices/system/node/has_cpu");
  if (node_ids.empty())
    node_ids = topology_detail::read_index_list("/sys/devices/system/node/online");

  for (int const node_id : node_ids)
    {
      auto const cpu_indices
          = topology_detail::read_index_list("/sys/devices/system/node/node" + std::to_string(node_id) + "/cpulist");
      if (cpu_indices.empty())
        continue;
      std::vector<cpu_id> cpus;
      cpus.reserve(cpu_indices.size());
      for (int const cpu : cpu_indices)
        cpus.emplace_back(cpu);
      topo.node_to_cpus.push_back(std::move(cpus));
    }

  if (topo.node_to_cpus.empty())
    {
      topo.node_to_cpus = { {} };
      for (std::size_t i = 0; i < topo.cpu_count; ++i)
        topo.node_to_cpus[0].emplace_back(static_cast<std::int64_t>(i));
    }
  topo.numa_nodes = topo.node_to_cpus.size();
#endif
  return topo;
}

/**
 * @brief Build a thread_affinity for the given NUMA node using a
 * pre-read topology.
 *
 * @param topo             Pre-read topology snapshot.
 * @param node_index       NUMA node index (wraps if out of range).
 * @param thread_index     Used to select CPU(s) within the node.
 * @param threads_per_node Number of CPUs to include per thread (default 1).
 */
inline auto
affinity_for_node(cpu_topology const& topo, int node_index, int thread_index, int threads_per_node = 1)
    -> thread_affinity
{
  auto const available_nodes = (std::min)(topo.numa_nodes, topo.node_to_cpus.size());
  if (available_nodes == 0 || threads_per_node <= 0)
    return {};
  auto const wrapped_index = [](int value, std::size_t count)
    {
      if (value >= 0)
        return static_cast<std::size_t>(value) % count;
      auto const magnitude = static_cast<std::uint64_t>(-(static_cast<std::int64_t>(value) + 1)) + 1;
      auto const remainder = static_cast<std::size_t>(magnitude % count);
      return remainder == 0 ? std::size_t{ 0 } : count - remainder;
    };
  auto const n = wrapped_index(node_index, available_nodes);
  auto const& cpus = topo.node_to_cpus[n];
  thread_affinity aff;
  if (cpus.empty())
    return aff;

  auto const cpu_count = cpus.size();
  auto const first = wrapped_index(thread_index, cpu_count);
  cpu_id const cpu = cpus[first];
  aff.add_cpu(cpu);
  for (int k = 1; k < threads_per_node; ++k)
    {
      cpu_id const extra = cpus[(first + static_cast<std::size_t>(k)) % cpu_count];
      aff.add_cpu(extra);
    }
  return aff;
}

/**
 * @brief Build a thread_affinity for the given NUMA node.
 *
 * Calls read_topology() internally on every invocation (no caching).
 *
 * @param node_index       NUMA node index (wraps if out of range).
 * @param thread_index     Used to select CPU(s) within the node.
 * @param threads_per_node Number of CPUs to include per thread (default 1).
 */
inline auto
affinity_for_node(int node_index, int thread_index, int threads_per_node = 1) -> thread_affinity
{
  return affinity_for_node(read_topology(), node_index, thread_index, threads_per_node);
}

/**
 * @brief Distribute thread affinities across NUMA nodes in round-robin order.
 *
 * Uses a pre-read topology to avoid repeated sysfs access.
 *
 * @param topo        Pre-read topology snapshot.
 * @param num_threads Number of affinity masks to generate.
 * @return Vector of @p num_threads thread_affinity objects.
 */
inline auto
distribute_affinities_by_numa(cpu_topology const& topo, size_t num_threads) -> std::vector<thread_affinity>
{
  std::vector<thread_affinity> result;
  result.reserve(num_threads);
  for (size_t i = 0; i < num_threads; ++i)
    {
      int node = (topo.numa_nodes > 0) ? static_cast<int>(i % topo.numa_nodes) : 0;
      result.push_back(affinity_for_node(topo, node, static_cast<int>(i)));
    }
  return result;
}

/**
 * @brief Distribute thread affinities across NUMA nodes in round-robin order.
 *
 * Returns one thread_affinity per thread, cycling through NUMA nodes
 * so that consecutive threads are spread across different nodes.
 *
 * @param num_threads Number of affinity masks to generate.
 * @return Vector of @p num_threads thread_affinity objects.
 */
inline auto
distribute_affinities_by_numa(size_t num_threads) -> std::vector<thread_affinity>
{
  return distribute_affinities_by_numa(read_topology(), num_threads);
}

} // namespace threadschedule::advanced
