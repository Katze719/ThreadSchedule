#include <benchmark/benchmark.h>
#include <threadschedule/thread_registry.hpp>
#include <string>
#include <vector>

#if !defined(THREADSCHEDULE_HAS_REFLECTION) || !THREADSCHEDULE_HAS_REFLECTION
#error "reflection_registry_benchmarks.cpp requires THREADSCHEDULE_HAS_REFLECTION"
#endif

using namespace threadschedule;

namespace
{

auto make_entries(std::size_t count) -> std::vector<RegisteredThreadInfo>
{
    std::vector<RegisteredThreadInfo> entries;
    entries.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        RegisteredThreadInfo info{};
        info.tid = static_cast<Tid>(index + 1);
        info.name = "worker-" + std::to_string(index);
        info.componentTag = (index % 3 == 0) ? "io" : ((index % 3 == 1) ? "compute" : "scheduler");
        info.alive = (index % 5) != 0;
        entries.push_back(std::move(info));
    }
    return entries;
}

} // namespace

static void BM_QueryView_FilterMapName(benchmark::State& state)
{
    ThreadRegistry::QueryView view(make_entries(static_cast<std::size_t>(state.range(0))));
    for (auto _ : state)
    {
        auto names = view.filter([](RegisteredThreadInfo const& entry) { return entry.componentTag == "io"; })
                         .map([](RegisteredThreadInfo const& entry) { return entry.name; });
        benchmark::DoNotOptimize(names);
    }
}

static void BM_QueryView_ReflectionWhereProjectName(benchmark::State& state)
{
    ThreadRegistry::QueryView view(make_entries(static_cast<std::size_t>(state.range(0))));
    for (auto _ : state)
    {
        auto names =
            view.where<registered_thread_fields::componentTag()>("io").project<registered_thread_fields::name()>();
        benchmark::DoNotOptimize(names);
    }
}

static void BM_QueryView_FindIf(benchmark::State& state)
{
    ThreadRegistry::QueryView view(make_entries(static_cast<std::size_t>(state.range(0))));
    for (auto _ : state)
    {
        auto found = view.find_if([](RegisteredThreadInfo const& entry) { return entry.name == "worker-42"; });
        benchmark::DoNotOptimize(found);
    }
}

static void BM_QueryView_ReflectionFindBy(benchmark::State& state)
{
    ThreadRegistry::QueryView view(make_entries(static_cast<std::size_t>(state.range(0))));
    for (auto _ : state)
    {
        auto found = view.find_by<registered_thread_fields::name()>(std::string("worker-42"));
        benchmark::DoNotOptimize(found);
    }
}

BENCHMARK(BM_QueryView_FilterMapName)->Arg(256)->Arg(4096)->Arg(16384);
BENCHMARK(BM_QueryView_ReflectionWhereProjectName)->Arg(256)->Arg(4096)->Arg(16384);
BENCHMARK(BM_QueryView_FindIf)->Arg(256)->Arg(4096)->Arg(16384);
BENCHMARK(BM_QueryView_ReflectionFindBy)->Arg(256)->Arg(4096)->Arg(16384);

BENCHMARK_MAIN();
