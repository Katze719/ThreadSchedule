#pragma once

#include "expected.hpp"
#include <algorithm>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#include <sys/resource.h>
#endif

namespace threadschedule
{
// expected/result are provided by expected.hpp

/**
 * @brief Enumeration of available scheduling policies
 */
enum class SchedulingPolicy
{
#ifdef _WIN32
    // Windows doesn't have the same scheduling policies as Linux
    // We'll use generic values
    OTHER = 0, ///< Standard scheduling
    FIFO = 1,  ///< First in, first out
    RR = 2,    ///< Round-robin
    BATCH = 3, ///< For batch style execution
    IDLE = 4   ///< For very low priority background tasks
#else
    OTHER = SCHED_OTHER, ///< Standard round-robin time-sharing
    FIFO = SCHED_FIFO,   ///< First in, first out
    RR = SCHED_RR,       ///< Round-robin
    BATCH = SCHED_BATCH, ///< For batch style execution
    IDLE = SCHED_IDLE,   ///< For very low priority background tasks
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
    constexpr explicit ThreadPriority(int priority = 0) : priority_(std::clamp(priority, min_priority, max_priority))
    {
    }

    [[nodiscard]] constexpr auto value() const noexcept -> int
    {
        return priority_;
    }
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        return priority_ >= min_priority && priority_ <= max_priority;
    }

    // Factory methods for common priorities
    static constexpr auto lowest() -> ThreadPriority
    {
        return ThreadPriority(min_priority);
    }
    static constexpr auto normal() -> ThreadPriority
    {
        return ThreadPriority(0);
    }
    static constexpr auto highest() -> ThreadPriority
    {
        return ThreadPriority(max_priority);
    }

    // Comparison operators
    [[nodiscard]] auto operator==(ThreadPriority const& other) const -> bool
    {
        return priority_ == other.priority_;
    }
    [[nodiscard]] auto operator!=(ThreadPriority const& other) const -> bool
    {
        return priority_ != other.priority_;
    }
    [[nodiscard]] auto operator<(ThreadPriority const& other) const -> bool
    {
        return priority_ < other.priority_;
    }
    [[nodiscard]] auto operator<=(ThreadPriority const& other) const -> bool
    {
        return priority_ <= other.priority_;
    }
    [[nodiscard]] auto operator>(ThreadPriority const& other) const -> bool
    {
        return priority_ > other.priority_;
    }
    [[nodiscard]] auto operator>=(ThreadPriority const& other) const -> bool
    {
        return priority_ >= other.priority_;
    }

    [[nodiscard]] auto to_string() const -> std::string
    {
        std::ostringstream oss;
        oss << "ThreadPriority(" << priority_ << ")";
        return oss.str();
    }

  private:
    static constexpr int min_priority = -20;
    static constexpr int max_priority = 19;
    int priority_;
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
        group_ = 0;
        mask_ = 0;
#else
        CPU_ZERO(&cpuset_);
#endif
    }

    explicit ThreadAffinity(std::vector<int> const& cpus) : ThreadAffinity()
    {
        for (int cpu : cpus)
        {
            add_cpu(cpu);
        }
    }

    // Adds a CPU index. On Windows, indices >= 64 select group = cpu/64 automatically.
    void add_cpu(int cpu)
    {
#ifdef _WIN32
        if (cpu < 0)
            return;
        WORD g = static_cast<WORD>(cpu / 64);
        int bit = cpu % 64;
        if (!has_any())
        {
            group_ = g;
        }
        if (g != group_)
        {
            // Single-group affinity object: ignore CPUs from other groups
            return;
        }
        mask_ |= (static_cast<unsigned long long>(1) << bit);
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
        if (cpu < 0)
            return;
        WORD g = static_cast<WORD>(cpu / 64);
        int bit = cpu % 64;
        if (g == group_)
        {
            mask_ &= ~(static_cast<unsigned long long>(1) << bit);
        }
#else
        if (cpu >= 0 && cpu < CPU_SETSIZE)
        {
            CPU_CLR(cpu, &cpuset_);
        }
#endif
    }

    [[nodiscard]] auto is_set(int cpu) const -> bool
    {
#ifdef _WIN32
        if (cpu < 0)
            return false;
        WORD g = static_cast<WORD>(cpu / 64);
        int bit = cpu % 64;
        return g == group_ && (mask_ & (static_cast<unsigned long long>(1) << bit)) != 0;
#else
        return cpu >= 0 && cpu < CPU_SETSIZE && CPU_ISSET(cpu, &cpuset_);
#endif
    }

    [[nodiscard]] auto has_cpu(int cpu) const -> bool
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

    [[nodiscard]] auto get_cpus() const -> std::vector<int>
    {
        std::vector<int> cpus;
#ifdef _WIN32
        for (int i = 0; i < 64; ++i)
        {
            if (mask_ & (static_cast<unsigned long long>(1) << i))
            {
                cpus.push_back(static_cast<int>(group_) * 64 + i);
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
    [[nodiscard]] unsigned long long get_mask() const
    {
        return mask_;
    }
    [[nodiscard]] WORD get_group() const
    {
        return group_;
    }
    [[nodiscard]] bool has_any() const
    {
        return mask_ != 0;
    }
#else
    [[nodiscard]] auto native_handle() const -> cpu_set_t const&
    {
        return cpuset_;
    }
#endif

    [[nodiscard]] auto to_string() const -> std::string
    {
        auto cpus = get_cpus();
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
    WORD group_;
    unsigned long long mask_;
#else
    cpu_set_t cpuset_;
#endif
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

    static expected<sched_param_win, std::error_code> create_for_policy(SchedulingPolicy policy,
                                                                        ThreadPriority priority)
    {
        sched_param_win param{};
        // On Windows, priority is directly used
        param.sched_priority = priority.value();
        return param;
    }

    static expected<int, std::error_code> get_priority_range(SchedulingPolicy policy)
    {
        // Windows thread priorities range from -15 to +15
        return 30;
    }
#else
    static auto create_for_policy(SchedulingPolicy policy, ThreadPriority priority)
        -> expected<sched_param, std::error_code>
    {
        sched_param param{};

        int const policy_int = static_cast<int>(policy);
        int const min_prio = sched_get_priority_min(policy_int);
        int const max_prio = sched_get_priority_max(policy_int);

        if (min_prio == -1 || max_prio == -1)
        {
            return unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        param.sched_priority = std::clamp(priority.value(), min_prio, max_prio);
        return param;
    }

    static auto get_priority_range(SchedulingPolicy policy) -> expected<int, std::error_code>
    {
        int const policy_int = static_cast<int>(policy);
        int const min_prio = sched_get_priority_min(policy_int);
        int const max_prio = sched_get_priority_max(policy_int);

        if (min_prio == -1 || max_prio == -1)
        {
            return unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        return max_prio - min_prio;
    }
#endif
};

/**
 * @brief String conversion utilities
 */
inline auto to_string(SchedulingPolicy policy) -> std::string
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
