#pragma once

/**
 * @file topology.hpp
 * @brief Hardware topology helpers (CPU count, NUMA nodes) and affinity builders.
 *
 * Exposes lightweight discovery of CPU/NUMA topology and convenience
 * functions to construct NUMA-aware `ThreadAffinity` masks. On Linux,
 * NUMA nodes are detected via sysfs (nodeX/cpulist). On Windows, nodes
 * default to 1 and CPUs are assigned sequentially.
 */

#include "scheduler_policy.hpp"
#include <cctype>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <fstream>
#include <string>
#include <unistd.h>
#endif

namespace threadschedule
{

/**
 * @brief Snapshot of basic CPU/NUMA topology.
 */
struct CpuTopology
{
    int cpu_count{0};
    int numa_nodes{1};
    // Mapping: node -> list of CPUs
    std::vector<std::vector<int>> node_to_cpus;
};

/**
 * @brief Discover basic topology. Linux: reads /sys for NUMA nodes.
 *        Windows: single node, sequential CPU indices.
 */
inline auto read_topology() -> CpuTopology
{
    CpuTopology topo;
    topo.cpu_count = static_cast<int>(std::thread::hardware_concurrency());
    if (topo.cpu_count <= 0)
        topo.cpu_count = 1;

#ifdef _WIN32
    topo.numa_nodes = 1;
    topo.node_to_cpus = {{}};
    for (int i = 0; i < topo.cpu_count; ++i)
        topo.node_to_cpus[0].push_back(i);
#else
    // Try to detect NUMA nodes via sysfs
    int nodes = 0;
    for (;;)
    {
        std::string path = "/sys/devices/system/node/node" + std::to_string(nodes);
        if (access(path.c_str(), F_OK) != 0)
            break;
        ++nodes;
    }
    topo.numa_nodes = (nodes > 0) ? nodes : 1;
    topo.node_to_cpus.resize(topo.numa_nodes);

    if (nodes > 0)
    {
        for (int nodeIndex = 0; nodeIndex < nodes; ++nodeIndex)
        {
            std::string list_path = "/sys/devices/system/node/node" + std::to_string(nodeIndex) + "/cpulist";
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
                int startCpu = 0;
                bool got = false;
                while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) != 0))
                {
                    got = true;
                    startCpu = startCpu * 10 + (s[i] - '0');
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
                    int endCpu = 0;
                    bool gotb = false;
                    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) != 0))
                    {
                        gotb = true;
                        endCpu = endCpu * 10 + (s[i] - '0');
                        ++i;
                    }
                    if (gotb && endCpu >= startCpu)
                    {
                        for (int cpuIndex = startCpu; cpuIndex <= endCpu; ++cpuIndex)
                            topo.node_to_cpus[nodeIndex].push_back(cpuIndex);
                    }
                }
                else
                {
                    topo.node_to_cpus[nodeIndex].push_back(startCpu);
                }
                if (i < s.size() && s[i] == ',')
                    ++i;
            }
        }
    }
    else
    {
        topo.node_to_cpus = {{}};
        for (int i = 0; i < topo.cpu_count; ++i)
            topo.node_to_cpus[0].push_back(i);
    }
#endif
    return topo;
}

/**
 * @brief Build a `ThreadAffinity` for the given NUMA node.
 * @param node_index NUMA node index (wraps if out of range)
 * @param thread_index Used to select CPU(s) within node
 * @param threads_per_node Optionally include multiple CPUs per thread
 */
inline auto affinity_for_node(int node_index, int thread_index, int threads_per_node = 1) -> ThreadAffinity
{
    CpuTopology topo = read_topology();
    if (topo.numa_nodes <= 0)
        return {};
    int const n = (node_index % topo.numa_nodes + topo.numa_nodes) % topo.numa_nodes;
    auto const& cpus = topo.node_to_cpus[n];
    ThreadAffinity aff;
    if (cpus.empty())
        return aff;

    int const cpu = cpus[(thread_index) % static_cast<int>(cpus.size())];
    aff.add_cpu(cpu);
    // Optionally add more CPUs for the same thread if threads_per_node > 1
    for (int k = 1; k < threads_per_node; ++k)
    {
        int const extra = cpus[(thread_index + k) % static_cast<int>(cpus.size())];
        aff.add_cpu(extra);
    }
    return aff;
}

/**
 * @brief Distribute thread affinities across NUMA nodes in round-robin order.
 */
inline auto distribute_affinities_by_numa(size_t num_threads) -> std::vector<ThreadAffinity>
{
    CpuTopology topo = read_topology();
    std::vector<ThreadAffinity> result;
    result.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i)
    {
        int node = (topo.numa_nodes > 0) ? static_cast<int>(i % topo.numa_nodes) : 0;
        result.push_back(affinity_for_node(node, static_cast<int>(i)));
    }
    return result;
}

} // namespace threadschedule
