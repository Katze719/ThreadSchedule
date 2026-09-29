#pragma once

#include "thread_benchmark_build.hpp"

#include <threadschedule/this_thread.hpp>
#include <threadschedule/thread.hpp>
#include <threadschedule/thread_view.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__linux__)
#  include <sys/utsname.h>
#endif

namespace thread_bench
{
namespace ts = threadschedule;
using clock = std::chrono::steady_clock;
using json = nlohmann::json;

enum class thread_kind
{
  standard,
  direct,
  factory
};

struct scenario
{
  char const* id;
  char const* label;
  bool configured;
  thread_kind kind;
};

inline constexpr std::array<scenario, 6> scenarios{ {
    { "std_thread", "std::thread", false, thread_kind::standard },
    { "ts_thread", "ts::thread", false, thread_kind::direct },
    { "ts_create", "ts::thread::create", false, thread_kind::factory },
    { "std_thread_named", "std::thread + this_thread::set_name", true, thread_kind::standard },
    { "ts_thread_named", "ts::thread(config)", true, thread_kind::direct },
    { "ts_create_named", "ts::thread::create(config)", true, thread_kind::factory },
} };

// All runners forward the same callable type, including its capture footprint.
// The callback cannot throw while a std::thread is still joinable.
template <typename Function, typename OnCreated>
void
run_thread(scenario const& selected, ts::thread_config const& config, Function&& function, OnCreated&& on_created)
{
  static_assert(std::is_nothrow_invocable_v<OnCreated&>, "creation callback must not throw");
  if (selected.kind == thread_kind::standard)
    {
      std::thread worker(std::forward<Function>(function));
      on_created();
      worker.join();
    }
  else if (selected.kind == thread_kind::direct)
    {
      if (selected.configured)
        {
          ts::thread worker(config, std::forward<Function>(function));
          on_created();
          worker.join_or_throw();
        }
      else
        {
          ts::thread worker(std::forward<Function>(function));
          on_created();
          worker.join_or_throw();
        }
    }
  else
    {
      ts::thread worker;
      auto created = selected.configured ? ts::thread::create(config, std::forward<Function>(function))
                                         : ts::thread::create(std::forward<Function>(function));
      if (!created)
        throw std::system_error(created.error(), "thread::create");
      worker = std::move(*created);
      on_created();
      worker.join_or_throw();
    }
}

inline void
configure_worker(scenario const& selected, ts::thread_config const& config)
{
  if (selected.configured && selected.kind == thread_kind::standard)
    {
      auto configured = ts::this_thread::set_name(*config.get_name());
      if (!configured)
        throw std::system_error(configured.error(), "this_thread::set_name");
    }
}

struct startup_state
{
  scenario const& selected;
  ts::thread_config const& config;
  clock::time_point entry{};
  clock::time_point ready{};
  std::exception_ptr failure{};
};

// Used unchanged by timing and allocation binaries: one pointer of callable state.
struct startup_callable
{
  startup_state* state;

  void
  operator()() const noexcept
  {
    state->entry = clock::now();
    try
      {
        configure_worker(state->selected, state->config);
        state->ready = clock::now();
      }
    catch (...)
      {
        state->failure = std::current_exception();
      }
  }
};

inline auto
nanoseconds(clock::duration duration) -> double
{
  return std::chrono::duration<double, std::nano>(duration).count();
}

inline auto
summary(std::vector<double> values) -> json
{
  if (values.empty())
    throw std::invalid_argument("cannot summarize an empty sample");
  std::sort(values.begin(), values.end());
  auto const middle = values.size() / 2;
  double const median = values.size() % 2 == 0 ? (values[middle - 1] + values[middle]) / 2.0 : values[middle];
  // Nearest-rank p95, with a one-based rank.
  auto const rank = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(values.size())));
  return { { "median", median }, { "p95", values[rank - 1] } };
}

inline auto
sizes() -> json
{
  return json::array(
      { { { "type", "std::thread" }, { "bytes", sizeof(std::thread) }, { "alignment", alignof(std::thread) } },
        { { "type", "ts::thread" }, { "bytes", sizeof(ts::thread) }, { "alignment", alignof(ts::thread) } },
        { { "type", "ts::thread_view" },
          { "bytes", sizeof(ts::thread_view) },
          { "alignment", alignof(ts::thread_view) } },
        { { "type", "ts::result<ts::thread>" },
          { "bytes", sizeof(ts::result<ts::thread>) },
          { "alignment", alignof(ts::result<ts::thread>) } } });
}

inline auto
metadata() -> json
{
  json result{ { "build_type", THREAD_BENCH_BUILD_TYPE },
               { "compiler_flags", THREAD_BENCH_COMPILER_FLAGS },
               { "hardware_concurrency", std::thread::hardware_concurrency() },
               { "clock", "std::chrono::steady_clock" },
               { "clock_period_ns", 1e9 * static_cast<double>(clock::period::num) / clock::period::den } };
#if defined(__clang__)
  result["compiler"] = std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
  result["compiler"] = std::string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
  result["compiler"] = std::string("MSVC ") + std::to_string(_MSC_VER);
#else
  result["compiler"] = "unknown";
#endif
#if defined(_MSVC_LANG)
  result["cpp_standard"] = _MSVC_LANG;
#else
  result["cpp_standard"] = __cplusplus;
#endif
#if defined(__GLIBCXX__)
  result["standard_library"] = std::string("libstdc++ ") + std::to_string(__GLIBCXX__);
#elif defined(_LIBCPP_VERSION)
  result["standard_library"] = std::string("libc++ ") + std::to_string(_LIBCPP_VERSION);
#elif defined(_MSVC_STL_VERSION)
  result["standard_library"] = std::string("MSVC STL ") + std::to_string(_MSVC_STL_VERSION);
#else
  result["standard_library"] = "unknown";
#endif
  result["cpu"] = "unavailable";
#if defined(__linux__)
  utsname system{};
  if (uname(&system) == 0)
    result["os"] = std::string(system.sysname) + " " + system.release + " " + system.machine;
  std::ifstream cpu_info("/proc/cpuinfo");
  std::string line;
  while (std::getline(cpu_info, line))
    {
      if (line.compare(0, 10, "model name") == 0)
        {
          auto const separator = line.find(':');
          if (separator != std::string::npos)
            result["cpu"] = line.substr(line.find_first_not_of(" \t", separator + 1));
          break;
        }
    }
#elif defined(_WIN32)
  result["os"] = "Windows";
#else
  result["os"] = "unknown";
#endif
  auto const now = std::time(nullptr);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  std::ostringstream timestamp;
  timestamp << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  result["measured_at_utc"] = timestamp.str();
  return result;
}

inline void
write_json(std::string const& path, json const& value)
{
  std::ofstream output(path);
  output << value.dump(2) << '\n';
  if (!output)
    throw std::runtime_error("cannot write JSON: " + path);
}
} // namespace thread_bench
