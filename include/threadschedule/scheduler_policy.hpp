#pragma once

#include "concepts.hpp"
#include <algorithm>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
// Windows doesn't use sched.h, but we'll define compatible types
#else
#include <sched.h>
#include <sys/resource.h>
#endif

namespace threadschedule
{

/**
 * @brief Enumeration of available scheduling policies
 */
enum class SchedulingPolicy
{
#ifdef _WIN32
    // Windows doesn't have the same scheduling policies as Linux
    // We'll use generic values
    OTHER = 0, ///< Standard scheduling
    FIFO  = 1, ///< First in, first out
    RR    = 2, ///< Round-robin
    BATCH = 3, ///< For batch style execution
    IDLE  = 4 ///< For very low priority background tasks
#else
    OTHER = SCHED_OTHER, ///< Standard round-robin time-sharing
    FIFO  = SCHED_FIFO, ///< First in, first out
    RR    = SCHED_RR, ///< Round-robin
    BATCH = SCHED_BATCH, ///< For batch style execution
    IDLE  = SCHED_IDLE, ///< For very low priority background tasks
#ifdef SCHED_DEADLINE
    DEADLINE = SCHED_DEADLINE ///< Real-time deadline scheduling
#endif
#endif
};

/**
 * @brief Thread priority wrapper with validation
 */
class ThreadPriority
{
  public:
    template <typename T = int, std::enable_if_t<PriorityType<T>, int> = 0>
    constexpr explicit ThreadPriority(T priority = 0)
        : priority_(std::clamp(static_cast<int>(priority), min_priority, max_priority))
    {
    }

    constexpr int value() const noexcept
    {
        return priority_;
    }
    constexpr bool is_valid() const noexcept
    {
        return priority_ >= min_priority && priority_ <= max_priority;
    }

    // Factory methods for common priorities
    static constexpr ThreadPriority lowest()
    {
        return ThreadPriority(min_priority);
    }
    static constexpr ThreadPriority normal()
    {
        return ThreadPriority(0);
    }
    static constexpr ThreadPriority highest()
    {
        return ThreadPriority(max_priority);
    }

    // Comparison operators
    bool operator==(const ThreadPriority &other) const
    {
        return priority_ == other.priority_;
    }
    bool operator!=(const ThreadPriority &other) const
    {
        return priority_ != other.priority_;
    }
    bool operator<(const ThreadPriority &other) const
    {
        return priority_ < other.priority_;
    }
    bool operator<=(const ThreadPriority &other) const
    {
        return priority_ <= other.priority_;
    }
    bool operator>(const ThreadPriority &other) const
    {
        return priority_ > other.priority_;
    }
    bool operator>=(const ThreadPriority &other) const
    {
        return priority_ >= other.priority_;
    }

    std::string to_string() const
    {
        std::ostringstream oss;
        oss << "ThreadPriority(" << priority_ << ")";
        return oss.str();
    }

  private:
    static constexpr int min_priority = -20;
    static constexpr int max_priority = 19;
    int                  priority_;
};

/**
 * @brief CPU affinity management
 */
class ThreadAffinity
{
  public:
    ThreadAffinity()
    {
#ifdef _WIN32
        mask_ = 0;
#else
        CPU_ZERO(&cpuset_);
#endif
    }

    // Generic container constructor (works with vector, set, etc.)
    template <typename T, std::enable_if_t<CPUSetType<T>, int> = 0>
    explicit ThreadAffinity(const T &cpus) : ThreadAffinity()
    {
        for (auto cpu : cpus)
        {
            add_cpu(static_cast<int>(cpu));
        }
    }

    // Constructor for initializer lists
    explicit ThreadAffinity(std::initializer_list<int> cpus) : ThreadAffinity()
    {
        for (int cpu : cpus)
        {
            add_cpu(cpu);
        }
    }

    void add_cpu(int cpu)
    {
#ifdef _WIN32
        if (cpu >= 0 && cpu < 64)
        {
            mask_ |= (static_cast<unsigned long long>(1) << cpu);
        }
#else
        if (cpu >= 0 && cpu < CPU_SETSIZE)
        {
            CPU_SET(cpu, &cpuset_);
        }
#endif
    }

    void remove_cpu(int cpu)
    {
#ifdef _WIN32
        if (cpu >= 0 && cpu < 64)
        {
            mask_ &= ~(static_cast<unsigned long long>(1) << cpu);
        }
#else
        if (cpu >= 0 && cpu < CPU_SETSIZE)
        {
            CPU_CLR(cpu, &cpuset_);
        }
#endif
    }

    bool is_set(int cpu) const
    {
#ifdef _WIN32
        return cpu >= 0 && cpu < 64 && (mask_ & (static_cast<unsigned long long>(1) << cpu)) != 0;
#else
        return cpu >= 0 && cpu < CPU_SETSIZE && CPU_ISSET(cpu, &cpuset_);
#endif
    }

    bool has_cpu(int cpu) const
    {
        return is_set(cpu);
    }

    void clear()
    {
#ifdef _WIN32
        mask_ = 0;
#else
        CPU_ZERO(&cpuset_);
#endif
    }

    std::vector<int> get_cpus() const
    {
        std::vector<int> cpus;
#ifdef _WIN32
        for (int i = 0; i < 64; ++i)
        {
            if (mask_ & (static_cast<unsigned long long>(1) << i))
            {
                cpus.push_back(i);
            }
        }
#else
        for (int i = 0; i < CPU_SETSIZE; ++i)
        {
            if (CPU_ISSET(i, &cpuset_))
            {
                cpus.push_back(i);
            }
        }
#endif
        return cpus;
    }

#ifdef _WIN32
    unsigned long long get_mask() const
    {
        return mask_;
    }
#else
    const cpu_set_t &native_handle() const
    {
        return cpuset_;
    }
#endif

    std::string to_string() const
    {
        auto               cpus = get_cpus();
        std::ostringstream oss;
        oss << "ThreadAffinity({";
        for (size_t i = 0; i < cpus.size(); ++i)
        {
            if (i > 0)
                oss << ", ";
            oss << cpus[i];
        }
        oss << "})";
        return oss.str();
    }

  private:
#ifdef _WIN32
    unsigned long long mask_;
#else
    cpu_set_t cpuset_;
#endif
};

/**
 * @brief Simple result type as std::expected replacement
 */
template <typename T, typename E> class result
{
  public:
    result(const T &value) : has_value_(true)
    {
        new (&value_) T(value);
    }

    result(const E &error) : has_value_(false)
    {
        new (&error_) E(error);
    }

    ~result()
    {
        if (has_value_)
        {
            value_.~T();
        }
        else
        {
            error_.~E();
        }
    }

    bool has_value() const
    {
        return has_value_;
    }
    const T &value() const
    {
        return value_;
    }
    const E &error() const
    {
        return error_;
    }

  private:
    bool has_value_;
    union {
        T value_;
        E error_;
    };
};

/**
 * @brief Scheduler parameter utilities
 */
class SchedulerParams
{
  public:
#ifdef _WIN32
    // Windows doesn't use sched_param, but we'll define a compatible type
    struct sched_param_win
    {
        int sched_priority;
    };

    static result<sched_param_win, std::error_code> create_for_policy(SchedulingPolicy policy, ThreadPriority priority)
    {
        sched_param_win param{};
        // On Windows, priority is directly used
        param.sched_priority = priority.value();
        return param;
    }

    static result<int, std::error_code> get_priority_range(SchedulingPolicy policy)
    {
        // Windows thread priorities range from -15 to +15
        return 30;
    }
#else
    static result<sched_param, std::error_code> create_for_policy(SchedulingPolicy policy, ThreadPriority priority)
    {
        sched_param param{};

        const int policy_int = static_cast<int>(policy);
        const int min_prio   = sched_get_priority_min(policy_int);
        const int max_prio   = sched_get_priority_max(policy_int);

        if (min_prio == -1 || max_prio == -1)
        {
            return std::make_error_code(std::errc::invalid_argument);
        }

        param.sched_priority = std::clamp(priority.value(), min_prio, max_prio);
        return param;
    }

    static result<int, std::error_code> get_priority_range(SchedulingPolicy policy)
    {
        const int policy_int = static_cast<int>(policy);
        const int min_prio   = sched_get_priority_min(policy_int);
        const int max_prio   = sched_get_priority_max(policy_int);

        if (min_prio == -1 || max_prio == -1)
        {
            return std::make_error_code(std::errc::invalid_argument);
        }

        return max_prio - min_prio;
    }
#endif
};

/**
 * @brief String conversion utilities
 */
std::string to_string(SchedulingPolicy policy)
{
    switch (policy)
    {
    case SchedulingPolicy::OTHER:
        return "OTHER";
    case SchedulingPolicy::FIFO:
        return "FIFO";
    case SchedulingPolicy::RR:
        return "RR";
    case SchedulingPolicy::BATCH:
        return "BATCH";
    case SchedulingPolicy::IDLE:
        return "IDLE";
#if defined(SCHED_DEADLINE) && !defined(_WIN32)
    case SchedulingPolicy::DEADLINE:
        return "DEADLINE";
#endif
    default:
        return "UNKNOWN";
    }
}

} // namespace threadschedule
