#pragma once

/**
 * @file topology.hpp
 * @brief Hardware topology helpers (CPU count, NUMA nodes) and affinity
 * builders.
 *
 * Exposes lightweight discovery of CPU/NUMA topology and convenience
 * functions to construct NUMA-aware `native_thread_affinity` masks. On Linux,
 * NUMA nodes are detected via sysfs (nodeX/cpulist). On Windows, nodes
 * default to 1 and CPUs are assigned sequentially.
 */

#include "scheduler_policy.hpp"
#include <cctype>
#include <thread>
#include <vector>

#ifndef _WIN32
#  include <fstream>
#  include <string>
#  include <unistd.h>
#endif

namespace threadschedule
{

/**
 * @brief Snapshot of basic CPU/NUMA topology.
 *
 * Value type (copyable). Populated by read_topology().
 *
 * - @c cpu_count: total logical CPUs (from @c
 * std::thread::hardware_concurrency).
 * - @c numa_nodes: number of NUMA nodes (always 1 on Windows; detected
 *   via @c /sys/devices/system/node/ on Linux).
 * - @c node_to_cpus: mapping from NUMA node index to the set of
 *   logical CPU indices belonging to that node.
 */
struct cpu_topology
{
  int cpu_count{ 0 };
  int numa_nodes{ 1 };
  std::vector<std::vector<int>> node_to_cpus;
};

/**
 * @brief Discover basic topology. Linux: reads /sys for NUMA nodes.
 *        Windows: single node, sequential CPU indices.
 *
 * Called frequently by chaos/affinity helpers. The result is not
 * cached internally - consider caching the returned cpu_topology
 * yourself if performance of repeated calls matters.
 */
inline auto
read_topology() -> cpu_topology
{
  cpu_topology topo;
  topo.cpu_count = static_cast<int>(std::thread::hardware_concurrency());
  if (topo.cpu_count <= 0)
    topo.cpu_count = 1;

#ifdef _WIN32
  topo.numa_nodes = 1;
  topo.node_to_cpus = { {} };
  for (int i = 0; i < topo.cpu_count; ++i)
    topo.node_to_cpus[0].push_back(i);
#else
  // Try to detect NUMA nodes via sysfs
  int nodes = 0;
  for (;;)
    {
      std::string path
          = "/sys/devices/system/node/node" + std::to_string(nodes);
      if (access(path.c_str(), F_OK) != 0)
        break;
      ++nodes;
    }
  topo.numa_nodes = (nodes > 0) ? nodes : 1;
  topo.node_to_cpus.resize(topo.numa_nodes);

  if (nodes > 0)
    {
      for (int node_index = 0; node_index < nodes; ++node_index)
        {
          std::string list_path = "/sys/devices/system/node/node"
                                  + std::to_string(node_index) + "/cpulist";
          std::ifstream in(list_path);
          if (!in)
            continue;
          std::string s;
          std::getline(in, s);
          // Parse cpulist like "0-3,8-11"
          size_t i = 0;
          while (i < s.size())
            {
              // read number
              int start_cpu = 0;
              bool got = false;
              while (i < s.size()
                     && (std::isdigit(static_cast<unsigned char>(s[i])) != 0))
                {
                  got = true;
                  start_cpu = start_cpu * 10 + (s[i] - '0');
                  ++i;
                }
              if (!got)
                {
                  ++i;
                  continue;
                }
              if (i < s.size() && s[i] == '-')
                {
                  ++i;
                  int end_cpu = 0;
                  bool gotb = false;
                  while (
                      i < s.size()
                      && (std::isdigit(static_cast<unsigned char>(s[i])) != 0))
                    {
                      gotb = true;
                      end_cpu = end_cpu * 10 + (s[i] - '0');
                      ++i;
                    }
                  if (gotb && end_cpu >= start_cpu)
                    {
                      for (int cpu_index = start_cpu; cpu_index <= end_cpu;
                           ++cpu_index)
                        topo.node_to_cpus[node_index].push_back(cpu_index);
                    }
                }
              else
                {
                  topo.node_to_cpus[node_index].push_back(start_cpu);
                }
              if (i < s.size() && s[i] == ',')
                ++i;
            }
        }
    }
  else
    {
      topo.node_to_cpus = { {} };
      for (int i = 0; i < topo.cpu_count; ++i)
        topo.node_to_cpus[0].push_back(i);
    }
#endif
  return topo;
}

/**
 * @brief Build a native_thread_affinity for the given NUMA node using a
 * pre-read topology.
 *
 * @param topo             Pre-read topology snapshot.
 * @param node_index       NUMA node index (wraps if out of range).
 * @param thread_index     Used to select CPU(s) within the node.
 * @param threads_per_node Number of CPUs to include per thread (default 1).
 */
inline auto
affinity_for_node(cpu_topology const& topo, int node_index, int thread_index,
                  int threads_per_node = 1) -> native_thread_affinity
{
  if (topo.numa_nodes <= 0)
    return {};
  int const n
      = (node_index % topo.numa_nodes + topo.numa_nodes) % topo.numa_nodes;
  auto const& cpus = topo.node_to_cpus[n];
  native_thread_affinity aff;
  if (cpus.empty())
    return aff;

  int const cpu = cpus[(thread_index) % static_cast<int>(cpus.size())];
  aff.add_cpu(cpu);
  for (int k = 1; k < threads_per_node; ++k)
    {
      int const extra
          = cpus[(thread_index + k) % static_cast<int>(cpus.size())];
      aff.add_cpu(extra);
    }
  return aff;
}

/**
 * @brief Build a native_thread_affinity for the given NUMA node.
 *
 * Calls read_topology() internally on every invocation (no caching).
 *
 * @param node_index       NUMA node index (wraps if out of range).
 * @param thread_index     Used to select CPU(s) within the node.
 * @param threads_per_node Number of CPUs to include per thread (default 1).
 */
inline auto
affinity_for_node(int node_index, int thread_index, int threads_per_node = 1)
    -> native_thread_affinity
{
  return affinity_for_node(read_topology(), node_index, thread_index,
                           threads_per_node);
}

/**
 * @brief Distribute thread affinities across NUMA nodes in round-robin order.
 *
 * Uses a pre-read topology to avoid repeated sysfs access.
 *
 * @param topo        Pre-read topology snapshot.
 * @param num_threads Number of affinity masks to generate.
 * @return Vector of @p num_threads native_thread_affinity objects.
 */
inline auto
distribute_affinities_by_numa(cpu_topology const& topo, size_t num_threads)
    -> std::vector<native_thread_affinity>
{
  std::vector<native_thread_affinity> result;
  result.reserve(num_threads);
  for (size_t i = 0; i < num_threads; ++i)
    {
      int node
          = (topo.numa_nodes > 0) ? static_cast<int>(i % topo.numa_nodes) : 0;
      result.push_back(affinity_for_node(topo, node, static_cast<int>(i)));
    }
  return result;
}

/**
 * @brief Distribute thread affinities across NUMA nodes in round-robin order.
 *
 * Returns one native_thread_affinity per thread, cycling through NUMA nodes
 * so that consecutive threads are spread across different nodes.
 *
 * @param num_threads Number of affinity masks to generate.
 * @return Vector of @p num_threads native_thread_affinity objects.
 */
inline auto
distribute_affinities_by_numa(size_t num_threads)
    -> std::vector<native_thread_affinity>
{
  return distribute_affinities_by_numa(read_topology(), num_threads);
}

} // namespace threadschedule
