#include <gtest/gtest.h>
#include <threadschedule/threadschedule.hpp>
#include <array>
#include <set>
#include <string_view>

#if !defined(THREADSCHEDULE_HAS_REFLECTION) || !THREADSCHEDULE_HAS_REFLECTION
#error "reflection_test.cpp requires THREADSCHEDULE_HAS_REFLECTION"
#endif

using namespace threadschedule;

TEST(ReflectionApiTest, ExposesMetadataForLibraryTypes)
{
    static_assert(reflect::enabled);
    static_assert(reflect::field_count<RegisteredThreadInfo>() == 6);
    static_assert(reflect::field_count<ThreadProfile>() == 4);
    static_assert(reflect::field_count<ChaosConfig>() == 3);
    static_assert(reflect::field_count<HighPerformancePool::Statistics>() == 7);
    static_assert(reflect::field_count<ThreadPoolBase::Statistics>() == 6);
    static_assert(reflect::field_name<RegisteredThreadInfo, 0>() == "tid");
    static_assert(reflect::field_name<RegisteredThreadInfo, 3>() == "componentTag");
    static_assert(reflect::field_name<ThreadProfile, 1>() == "policy");
    static_assert(reflect::field_name<ChaosConfig, 1>() == "priority_jitter");
    constexpr auto registry_field_names = reflect::field_names<RegisteredThreadInfo>();
    static_assert(registry_field_names.size() == 6);
    static_assert(std::string_view(registry_field_names[2]) == "name");
    static_assert(reflect::type_name<ThreadProfile>().contains("ThreadProfile"));
}

TEST(ReflectionApiTest, VisitFieldsAndGetWorkForPublicStructs)
{
    ThreadProfile profile{"latency", SchedulingPolicy::RR, ThreadPriority{3}, std::nullopt};
    std::array<std::string_view, 4> expected = {"name", "policy", "priority", "affinity"};
    std::size_t index = 0;

    reflect::visit_fields(profile, [&](std::string_view name, auto&) {
        ASSERT_LT(index, expected.size());
        EXPECT_EQ(name, expected[index]);
        ++index;
    });

    EXPECT_EQ(index, expected.size());
    EXPECT_EQ(reflect::get<reflect::field_info<ThreadProfile, 0>()>(profile), "latency");
}

TEST(ReflectionApiTest, ProjectValueBuildsCompactResults)
{
    RegisteredThreadInfo info{};
    info.tid = Tid{11};
    info.name = "alpha";
    info.componentTag = "io";
    info.alive = true;

    auto tuple =
        reflect::project_value<registered_thread_fields::name(), registered_thread_fields::componentTag()>(info);

    EXPECT_EQ(std::get<0>(tuple), "alpha");
    EXPECT_EQ(std::get<1>(tuple), "io");
    EXPECT_TRUE(reflect::is_field_of_v<registered_thread_fields::alive(), RegisteredThreadInfo>);
}
