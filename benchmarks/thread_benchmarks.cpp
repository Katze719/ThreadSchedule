// Thread startup and steady-state work measured separately, without an added startup gate.
#include "thread_benchmark_common.hpp"

#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
namespace tb = thread_bench;
using tb::json;

struct options
{
  std::string output = "thread-timing.json";
  std::size_t warmup = 200;
  std::size_t samples = 2000;
  std::size_t work_samples = 30;
  std::size_t work_iterations = 0;
  bool self_test = false;
};

auto
parse_count(std::string const& text) -> std::size_t
{
  if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
    throw std::invalid_argument("expected a non-negative integer: " + text);
  std::size_t consumed = 0;
  auto const value = std::stoull(text, &consumed);
  if (consumed != text.size() || value > std::numeric_limits<std::size_t>::max())
    throw std::invalid_argument("integer is out of range: " + text);
  return static_cast<std::size_t>(value);
}

auto
parse_options(int argc, char** argv) -> options
{
  options result;
  for (int index = 1; index < argc; ++index)
    {
      std::string const argument(argv[index]);
      if (argument == "--self-test")
        {
          result.self_test = true;
          continue;
        }
      if (++index == argc)
        throw std::invalid_argument("missing value for " + argument);
      std::string const value(argv[index]);
      if (argument == "--output")
        result.output = value;
      else if (argument == "--warmup")
        result.warmup = parse_count(value);
      else if (argument == "--samples")
        result.samples = parse_count(value);
      else if (argument == "--work-samples")
        result.work_samples = parse_count(value);
      else if (argument == "--work-iterations")
        result.work_iterations = parse_count(value);
      else
        throw std::invalid_argument("unknown option: " + argument);
    }
  if (result.samples == 0 || result.work_samples == 0)
    throw std::invalid_argument("--samples and --work-samples must be positive");
  return result;
}

// Every thread variant calls this one out-of-line kernel. Unsigned operations
// have defined wraparound semantics on all supported compilers.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
auto
work_kernel(std::uint64_t value, std::size_t iterations) noexcept -> std::uint64_t
{
  for (std::size_t index = 0; index < iterations; ++index)
    {
      value ^= value >> 12;
      value ^= value << 25;
      value ^= value >> 27;
      value *= UINT64_C(0x2545f4914f6cdd1d);
    }
  return value;
}

struct startup_sample
{
  double creation_ns;
  double entry_ns;
  double ready_ns;
  double lifecycle_ns;
};

auto
measure_startup(tb::scenario const& selected, tb::ts::thread_config const& config) -> startup_sample
{
  tb::startup_state state{ selected, config, {}, {}, {} };
  tb::clock::time_point created;
  auto const started = tb::clock::now();
  tb::run_thread(selected, config, tb::startup_callable{ &state },
                 [&created]() noexcept { created = tb::clock::now(); });
  auto const joined = tb::clock::now();
  if (state.failure)
    std::rethrow_exception(state.failure);
  return { tb::nanoseconds(created - started), tb::nanoseconds(state.entry - started),
           tb::nanoseconds(state.ready - started), tb::nanoseconds(joined - started) };
}

struct work_state
{
  tb::scenario const& selected;
  tb::ts::thread_config const& config;
  std::uint64_t seed;
  std::size_t iterations;
  double elapsed_ns = 0;
  std::uint64_t checksum = 0;
  std::exception_ptr failure;
};

struct work_sample
{
  double elapsed_ns;
  std::uint64_t checksum;
};

auto
measure_work(tb::scenario const& selected, tb::ts::thread_config const& config, std::uint64_t seed,
             std::size_t iterations) -> work_sample
{
  work_state state{ selected, config, seed, iterations, 0, 0, {} };
  tb::run_thread(
      selected, config,
      [&state]() noexcept
        {
          try
            {
              tb::configure_worker(state.selected, state.config);
              // One volatile load/store prevents moving or eliminating work across
              // the clocks; the arithmetic loop itself uses ordinary registers.
              volatile auto input = work_kernel(state.seed, 4096);
              auto const started = tb::clock::now();
              volatile auto output = work_kernel(input, state.iterations);
              auto const finished = tb::clock::now();
              state.elapsed_ns = tb::nanoseconds(finished - started);
              state.checksum = output;
            }
          catch (...)
            {
              state.failure = std::current_exception();
            }
        },
      []() noexcept {});
  if (state.failure)
    std::rethrow_exception(state.failure);
  return { state.elapsed_ns, state.checksum };
}

auto
calibrate(tb::ts::thread_config const& config, std::uint64_t seed) -> std::size_t
{
  std::size_t iterations = 16384;
  for (;;)
    {
      auto const sample = measure_work(tb::scenarios.front(), config, seed, iterations);
      if (sample.checksum != work_kernel(work_kernel(seed, 4096), iterations))
        throw std::runtime_error("calibration checksum mismatch");
      if (sample.elapsed_ns >= 5e6)
        return iterations;
      if (iterations > std::numeric_limits<std::size_t>::max() / 2)
        throw std::runtime_error("work iteration calibration overflow");
      iterations *= 2;
    }
}

struct measurements
{
  std::vector<double> creation_ns;
  std::vector<double> entry_ns;
  std::vector<double> ready_ns;
  std::vector<double> lifecycle_ns;
  std::vector<double> work_ns_per_iteration;
  std::string error;

  explicit measurements(options const& settings)
  {
    creation_ns.reserve(settings.samples);
    entry_ns.reserve(settings.samples);
    ready_ns.reserve(settings.samples);
    lifecycle_ns.reserve(settings.samples);
    work_ns_per_iteration.reserve(settings.work_samples);
  }

  void
  append(startup_sample const& sample)
  {
    creation_ns.push_back(sample.creation_ns);
    entry_ns.push_back(sample.entry_ns);
    ready_ns.push_back(sample.ready_ns);
    lifecycle_ns.push_back(sample.lifecycle_ns);
  }

  auto
  to_json(tb::scenario const& selected) const -> json
  {
    json result{ { "id", selected.id }, { "label", selected.label }, { "configured", selected.configured } };
    if (!error.empty())
      {
        result["status"] = "error";
        result["error"] = error;
        return result;
      }
    result["status"] = "ok";
    result["samples"] = { { "creation_ns", creation_ns },
                          { "entry_ns", entry_ns },
                          { "ready_ns", ready_ns },
                          { "lifecycle_ns", lifecycle_ns },
                          { "work_ns_per_iteration", work_ns_per_iteration } };
    result["metrics"] = { { "creation_ns", tb::summary(creation_ns) },
                          { "entry_ns", tb::summary(entry_ns) },
                          { "ready_ns", tb::summary(ready_ns) },
                          { "lifecycle_ns", tb::summary(lifecycle_ns) },
                          { "work_ns_per_iteration", tb::summary(work_ns_per_iteration) } };
    return result;
  }
};

// Rotate the first scenario and reverse direction on alternate rounds.
auto
scenario_index(std::size_t round, std::size_t offset) -> std::size_t
{
  auto const count = tb::scenarios.size();
  return (round % count + (round % 2 == 0 ? offset : count - offset)) % count;
}

void
collect_startup(options const& settings, tb::ts::thread_config const& config, std::vector<measurements>& results)
{
  auto collect = [&](std::size_t rounds, bool keep)
    {
      for (std::size_t round = 0; round < rounds; ++round)
        for (std::size_t offset = 0; offset < tb::scenarios.size(); ++offset)
          {
            auto const index = scenario_index(round, offset);
            if (!results[index].error.empty())
              continue;
            try
              {
                auto const sample = measure_startup(tb::scenarios[index], config);
                if (keep)
                  results[index].append(sample);
              }
            catch (std::exception const& error)
              {
                results[index].error = error.what();
              }
          }
    };
  collect(settings.warmup, false);
  collect(settings.samples, true);
}

void
collect_work(options const& settings, tb::ts::thread_config const& config, std::uint64_t seed,
             std::vector<measurements>& results)
{
  for (std::size_t round = 0; round < settings.work_samples; ++round)
    {
      auto const input = seed ^ (UINT64_C(0x9e3779b97f4a7c15) * (round + 1));
      auto const expected = work_kernel(work_kernel(input, 4096), settings.work_iterations);
      for (std::size_t offset = 0; offset < tb::scenarios.size(); ++offset)
        {
          auto const index = scenario_index(round, offset);
          if (!results[index].error.empty())
            continue;
          try
            {
              auto const sample = measure_work(tb::scenarios[index], config, input, settings.work_iterations);
              if (sample.checksum != expected)
                throw std::runtime_error("work checksum mismatch");
              results[index].work_ns_per_iteration.push_back(sample.elapsed_ns
                                                             / static_cast<double>(settings.work_iterations));
            }
          catch (std::exception const& error)
            {
              results[index].error = error.what();
            }
        }
    }
}

void
print_results(json const& report)
{
  std::cout << "Times in ns (median / p95; median ratio to matching std::thread baseline).\n";
  for (std::size_t index = 0; index < tb::scenarios.size(); ++index)
    {
      auto const& result = report["scenarios"][index];
      std::cout << '\n' << result["label"].get<std::string>() << '\n';
      if (result["status"] != "ok")
        {
          std::cout << "  ERROR: " << result["error"].get<std::string>() << '\n';
          continue;
        }
      auto const& baseline = report["scenarios"][index < 3 ? 0 : 3];
      for (auto const& metric : { "creation_ns", "entry_ns", "ready_ns", "lifecycle_ns", "work_ns_per_iteration" })
        {
          auto const& values = result["metrics"][metric];
          double const median = values["median"];
          std::cout << "  " << std::left << std::setw(24) << metric << std::right << std::fixed << std::setprecision(2)
                    << std::setw(12) << median << " / " << std::setw(12) << values["p95"].get<double>();
          if (baseline["status"] == "ok")
            std::cout << "  " << median / baseline["metrics"][metric]["median"].get<double>() << "x";
          std::cout << '\n';
        }
    }
  std::cout << "\nObject sizes (bytes / alignment):\n";
  for (auto const& size : report["sizes"])
    std::cout << "  " << size["type"].get<std::string>() << ": " << size["bytes"] << " / " << size["alignment"] << '\n';
}

void
self_test()
{
  auto require = [](bool condition, char const* message)
    {
      if (!condition)
        throw std::runtime_error(message);
    };
  require(tb::summary({ 4, 1, 3, 2 }) == json{ { "median", 2.5 }, { "p95", 4.0 } }, "even statistics failed");
  require(tb::summary({ 5, 1, 3 }) == json{ { "median", 3.0 }, { "p95", 5.0 } }, "odd statistics failed");
  require(tb::summary({ 7 }) == json{ { "median", 7.0 }, { "p95", 7.0 } }, "single-sample statistics failed");
  std::vector<double> values;
  for (int value = 1; value <= 20; ++value)
    values.push_back(value);
  require(tb::summary(values)["p95"] == 19.0, "nearest-rank p95 failed");
  bool empty_rejected = false;
  try
    {
      tb::summary({});
    }
  catch (std::invalid_argument const&)
    {
      empty_rejected = true;
    }
  require(empty_rejected, "empty statistics accepted");
  constexpr auto seed = UINT64_C(0x123456789abcdef0);
  require(work_kernel(seed, 0) == seed, "zero-iteration kernel failed");
  require(work_kernel(seed, 1) == UINT64_C(0xb7fb0288c5ee4339), "one-iteration kernel failed");
  require(work_kernel(seed, 4) == UINT64_C(0x4a55d29f8ef7d6f0), "kernel checksum failed");
  for (std::size_t round = 0; round < 12; ++round)
    {
      std::array<bool, tb::scenarios.size()> seen{};
      for (std::size_t offset = 0; offset < seen.size(); ++offset)
        seen[scenario_index(round, offset)] = true;
      require(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }), "scenario order failed");
    }
  std::cout << "Thread timing self-tests passed.\n";
}
} // namespace

auto
main(int argc, char** argv) -> int
{
  try
    {
      auto settings = parse_options(argc, argv);
      if (settings.self_test)
        {
          self_test();
          return 0;
        }
      json report{ { "schema_version", 1 }, { "kind", "timing" }, { "metadata", tb::metadata() } };
      tb::ts::thread_config config;
      config.set_name("ts-bench");
      auto const seed = static_cast<std::uint64_t>(tb::clock::now().time_since_epoch().count());
      std::vector<measurements> results;
      results.reserve(tb::scenarios.size());
      for (std::size_t index = 0; index < tb::scenarios.size(); ++index)
        results.emplace_back(settings);
      collect_startup(settings, config, results);
      if (settings.work_iterations == 0)
        settings.work_iterations = calibrate(config, seed);
      collect_work(settings, config, seed, results);
      report["parameters"] = { { "warmup", settings.warmup },
                               { "samples", settings.samples },
                               { "work_samples", settings.work_samples },
                               { "work_iterations", settings.work_iterations },
                               { "work_seed", seed },
                               { "configuration_name", "ts-bench" },
                               { "percentile_method", "nearest-rank" } };
      report["scenarios"] = json::array();
      bool failed = false;
      for (std::size_t index = 0; index < tb::scenarios.size(); ++index)
        {
          report["scenarios"].push_back(results[index].to_json(tb::scenarios[index]));
          failed = failed || !results[index].error.empty();
        }
      report["sizes"] = tb::sizes();
      tb::write_json(settings.output, report);
      print_results(report);
      std::cout << "\nJSON: " << settings.output << '\n';
      return failed ? 1 : 0;
    }
  catch (std::exception const& error)
    {
      std::cerr << "thread benchmark: " << error.what() << '\n';
      return 1;
    }
}
