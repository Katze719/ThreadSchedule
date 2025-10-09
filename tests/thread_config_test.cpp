#include <gtest/gtest.h>
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

class ThreadConfigTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

// ==================== ThreadPriority Tests ====================

TEST_F(ThreadConfigTest, ThreadPriorityDefaultConstruction)
{
    ThreadPriority priority;
    EXPECT_EQ(priority.value(), 0);
}

TEST_F(ThreadConfigTest, ThreadPriorityValueConstruction)
{
    ThreadPriority priority(10);
    EXPECT_EQ(priority.value(), 10);
}

TEST_F(ThreadConfigTest, ThreadPriorityFactoryMethods)
{
    auto lowest = ThreadPriority::lowest();
    auto normal = ThreadPriority::normal();
    auto highest = ThreadPriority::highest();

    EXPECT_LT(lowest.value(), normal.value());
    EXPECT_EQ(normal.value(), 0);
    EXPECT_LT(normal.value(), highest.value());
}

TEST_F(ThreadConfigTest, ThreadPriorityComparison)
{
    ThreadPriority p1(5);
    ThreadPriority p2(10);
    ThreadPriority p3(5);

    EXPECT_TRUE(p1 == p3);
    EXPECT_TRUE(p1 != p2);
    EXPECT_TRUE(p1 < p2);
    EXPECT_TRUE(p2 > p1);
    EXPECT_TRUE(p1 <= p2);
    EXPECT_TRUE(p1 <= p3);
    EXPECT_TRUE(p2 >= p1);
    EXPECT_TRUE(p1 >= p3);
}

TEST_F(ThreadConfigTest, ThreadPriorityToString)
{
    ThreadPriority normal = ThreadPriority::normal();
    std::string str = normal.to_string();

    EXPECT_FALSE(str.empty());
    EXPECT_NE(str.find("0"), std::string::npos);
}

TEST_F(ThreadConfigTest, ThreadPriorityMinMax)
{
    auto min_prio = ThreadPriority::lowest();
    auto max_prio = ThreadPriority::highest();

    EXPECT_LT(min_prio.value(), max_prio.value());
}

// ==================== ThreadAffinity Tests ====================

TEST_F(ThreadConfigTest, ThreadAffinityDefaultConstruction)
{
    ThreadAffinity affinity;
    // Default construction should have some valid state
}

TEST_F(ThreadConfigTest, ThreadAffinityCPUList)
{
    std::vector<int> cpus = {0, 1, 2};
    ThreadAffinity affinity(cpus);

    EXPECT_TRUE(affinity.is_set(0));
    EXPECT_TRUE(affinity.is_set(1));
    EXPECT_TRUE(affinity.is_set(2));
    EXPECT_FALSE(affinity.is_set(3));
}

TEST_F(ThreadConfigTest, ThreadAffinityAddRemove)
{
    ThreadAffinity affinity;

    affinity.add_cpu(0);
    EXPECT_TRUE(affinity.is_set(0));

    affinity.remove_cpu(0);
    EXPECT_FALSE(affinity.is_set(0));
}

TEST_F(ThreadConfigTest, ThreadAffinityAddMultiple)
{
    ThreadAffinity affinity;
    affinity.add_cpu(0);
    affinity.add_cpu(1);
    affinity.add_cpu(2);

    // Check added CPUs
    EXPECT_TRUE(affinity.is_set(0));
    EXPECT_TRUE(affinity.is_set(1));
    EXPECT_TRUE(affinity.is_set(2));
}

TEST_F(ThreadConfigTest, ThreadAffinityClear)
{
    ThreadAffinity affinity;
    affinity.add_cpu(0);
    affinity.add_cpu(1);
    affinity.clear();

    // Check that CPUs are cleared
    EXPECT_FALSE(affinity.is_set(0));
    EXPECT_FALSE(affinity.is_set(1));
}

TEST_F(ThreadConfigTest, ThreadAffinityGetCPUs)
{
    ThreadAffinity affinity;
    affinity.add_cpu(0);
    affinity.add_cpu(2);
    affinity.add_cpu(4);

    auto cpus = affinity.get_cpus();
    EXPECT_EQ(cpus.size(), 3);
    EXPECT_TRUE(std::find(cpus.begin(), cpus.end(), 0) != cpus.end());
    EXPECT_TRUE(std::find(cpus.begin(), cpus.end(), 2) != cpus.end());
    EXPECT_TRUE(std::find(cpus.begin(), cpus.end(), 4) != cpus.end());
}

TEST_F(ThreadConfigTest, ThreadAffinityToString)
{
    ThreadAffinity affinity;
    affinity.add_cpu(0);
    affinity.add_cpu(1);

    std::string str = affinity.to_string();
    EXPECT_FALSE(str.empty());
}

#ifndef _WIN32
TEST_F(ThreadConfigTest, ThreadAffinityNativeHandle)
{
    ThreadAffinity affinity;
    affinity.add_cpu(0);

    cpu_set_t const& cpuset = affinity.native_handle();
    EXPECT_TRUE(CPU_ISSET(0, &cpuset));
}
#endif

// ==================== SchedulingPolicy Tests ====================

TEST_F(ThreadConfigTest, SchedulingPolicyValues)
{
    auto other = SchedulingPolicy::OTHER;
    auto fifo = SchedulingPolicy::FIFO;
    auto rr = SchedulingPolicy::RR;
    // Just ensure they're different
    EXPECT_NE(static_cast<int>(other), static_cast<int>(fifo));
    EXPECT_NE(static_cast<int>(fifo), static_cast<int>(rr));
}

TEST_F(ThreadConfigTest, SchedulingPolicyToString)
{
    std::vector<SchedulingPolicy> policies = {SchedulingPolicy::OTHER, SchedulingPolicy::FIFO, SchedulingPolicy::RR};
#if defined(SCHED_DEADLINE) && !defined(_WIN32)
    policies.push_back(SchedulingPolicy::DEADLINE);
#endif

    for (auto policy : policies)
    {
        std::string str = to_string(policy);
        EXPECT_FALSE(str.empty());
    }
}

// ==================== SchedulerParams Tests ====================

TEST_F(ThreadConfigTest, SchedulerParamsCreation)
{
    auto params_result = SchedulerParams::create_for_policy(SchedulingPolicy::OTHER, ThreadPriority::normal());

    // Should succeed for OTHER policy
    EXPECT_TRUE(params_result.has_value());

    if (params_result.has_value())
    {
        auto const& params = params_result.value();
        // Verify it's a valid sched_param
#ifndef _WIN32
        EXPECT_GE(params.sched_priority, 0);
#endif
    }
}

#ifndef _WIN32
TEST_F(ThreadConfigTest, SchedulerParamsFIFO)
{
    auto params = SchedulerParams::create_for_policy(SchedulingPolicy::FIFO, ThreadPriority::highest());

    // May fail if we don't have permissions
    if (params.has_value())
    {
        EXPECT_GT(params.value().sched_priority, 0);
    }
}
#endif

// ==================== Integration Tests ====================

TEST_F(ThreadConfigTest, ApplyConfigToThread)
{
    std::atomic<bool> executed{false};

    ThreadWrapper thread([&executed]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        executed = true;
    });

    // Try to configure the thread (may fail without permissions)
    [[maybe_unused]] auto _namer = thread.set_name("test_config");
    [[maybe_unused]] auto _prio = thread.set_priority(ThreadPriority::normal());

    ThreadAffinity affinity;
    affinity.add_cpu(0);
    [[maybe_unused]] auto _aff = thread.set_affinity(affinity);

    thread.join();
    EXPECT_TRUE(executed);
}

TEST_F(ThreadConfigTest, ThreadConfigWithSchedulingPolicy)
{
    std::atomic<bool> executed{false};

    ThreadWrapper thread([&executed]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        executed = true;
    });

    // Try to set scheduling policy (may fail without permissions)
    [[maybe_unused]] auto _sched = thread.set_scheduling_policy(SchedulingPolicy::OTHER, ThreadPriority::normal());

    // Just ensure it doesn't crash
    thread.join();
    EXPECT_TRUE(executed);
}

// ==================== Nice Value Tests ====================

TEST_F(ThreadConfigTest, NiceValue)
{
    // Get current nice value
    auto current_nice = ThreadWrapper::get_nice_value();
    EXPECT_TRUE(current_nice.has_value());

    // Try to set nice value (may fail without permissions)
    bool result = ThreadWrapper::set_nice_value(0);

    // Restore if we changed it
    if (result && current_nice.has_value())
    {
        ThreadWrapper::set_nice_value(current_nice.value());
    }
}

// ==================== Factory Method Tests ====================

#ifndef _WIN32
TEST_F(ThreadConfigTest, PThreadWrapperFactory)
{
    std::atomic<bool> executed{false};

    auto thread = PThreadWrapper::create_with_config("test_pthread", SchedulingPolicy::OTHER, ThreadPriority::normal(),
                                                     [&executed]() {
                                                         std::this_thread::sleep_for(std::chrono::milliseconds(50));
                                                         executed = true;
                                                     });

    auto name = thread.get_name();
    // Name may or may not be set depending on permissions
    if (name.has_value())
    {
        EXPECT_EQ(name.value(), "test_pthread");
    }

    thread.join();
    EXPECT_TRUE(executed);
}
#endif
