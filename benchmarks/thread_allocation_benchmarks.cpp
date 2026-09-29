#include "thread_benchmark_common.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <malloc.h>
#endif

// Only this executable replaces allocation functions. The timing executable
// therefore pays none of the atomic-counter or allocation-hook overhead.
namespace allocation_tracking
{
std::atomic<bool> enabled{ false };
std::atomic<std::uint64_t> allocations{ 0 };
std::atomic<std::uint64_t> bytes{ 0 };

struct counts
{
  std::uint64_t allocations;
  std::uint64_t bytes;
};

class window
{
public:
  window() noexcept
  {
    allocations.store(0, std::memory_order_relaxed);
    bytes.store(0, std::memory_order_relaxed);
    enabled.store(true, std::memory_order_release);
  }

  window(window const&) = delete;
  auto operator=(window const&) -> window& = delete;

  ~window()
  {
    enabled.store(false, std::memory_order_release);
  }

  [[nodiscard]] auto
  finish() noexcept -> counts
  {
    enabled.store(false, std::memory_order_release);
    return { allocations.load(std::memory_order_relaxed), bytes.load(std::memory_order_relaxed) };
  }
};

void
record(std::size_t requested) noexcept
{
  if (enabled.load(std::memory_order_acquire))
    {
      allocations.fetch_add(1, std::memory_order_relaxed);
      bytes.fetch_add(requested, std::memory_order_relaxed);
    }
}

auto
allocate(std::size_t requested, std::size_t alignment = 0) -> void*
{
  auto const size = requested == 0 ? std::size_t{ 1 } : requested;
  for (;;)
    {
      void* storage = nullptr;
      if (alignment == 0)
        storage = std::malloc(size);
      else
        {
          // Aligned new also accepts fundamental alignments smaller than a
          // pointer, while the platform allocators require at least this much.
          auto const native_alignment = alignment < sizeof(void*) ? sizeof(void*) : alignment;
#if defined(_WIN32)
          storage = _aligned_malloc(size, native_alignment);
#else
          // Unlike aligned_alloc, posix_memalign does not require rounding the
          // requested size up to an alignment multiple (which could overflow).
          if (posix_memalign(&storage, native_alignment, size) != 0)
            storage = nullptr;
#endif
        }
      if (storage != nullptr)
        {
          record(requested);
          return storage;
        }
      auto const handler = std::get_new_handler();
      if (handler == nullptr)
        throw std::bad_alloc();
      handler();
    }
}

auto
allocate_nothrow(std::size_t requested, std::size_t alignment = 0) noexcept -> void*
{
  try
    {
      return allocate(requested, alignment);
    }
  catch (...)
    {
      return nullptr;
    }
}

void
free_aligned(void* storage) noexcept
{
#if defined(_WIN32)
  _aligned_free(storage);
#else
  std::free(storage);
#endif
}
} // namespace allocation_tracking

// All replaceable C++17 forms, including the matching nothrow cleanup forms.
// Placement new/delete taking a caller-provided void* are not replaceable.
auto
operator new(std::size_t size) -> void*
{
  return allocation_tracking::allocate(size);
}
auto
operator new[](std::size_t size) -> void*
{
  return allocation_tracking::allocate(size);
}
auto
operator new(std::size_t size, std::nothrow_t const&) noexcept -> void*
{
  return allocation_tracking::allocate_nothrow(size);
}
auto
operator new[](std::size_t size, std::nothrow_t const&) noexcept -> void*
{
  return allocation_tracking::allocate_nothrow(size);
}
void
operator delete(void* storage) noexcept
{
  std::free(storage);
}
void
operator delete[](void* storage) noexcept
{
  std::free(storage);
}
void
operator delete(void* storage, std::size_t) noexcept
{
  std::free(storage);
}
void
operator delete[](void* storage, std::size_t) noexcept
{
  std::free(storage);
}
void
operator delete(void* storage, std::nothrow_t const&) noexcept
{
  std::free(storage);
}
void
operator delete[](void* storage, std::nothrow_t const&) noexcept
{
  std::free(storage);
}
auto
operator new(std::size_t size, std::align_val_t alignment) -> void*
{
  return allocation_tracking::allocate(size, static_cast<std::size_t>(alignment));
}
auto
operator new[](std::size_t size, std::align_val_t alignment) -> void*
{
  return allocation_tracking::allocate(size, static_cast<std::size_t>(alignment));
}
auto
operator new(std::size_t size, std::align_val_t alignment, std::nothrow_t const&) noexcept -> void*
{
  return allocation_tracking::allocate_nothrow(size, static_cast<std::size_t>(alignment));
}
auto
operator new[](std::size_t size, std::align_val_t alignment, std::nothrow_t const&) noexcept -> void*
{
  return allocation_tracking::allocate_nothrow(size, static_cast<std::size_t>(alignment));
}
void
operator delete(void* storage, std::align_val_t) noexcept
{
  allocation_tracking::free_aligned(storage);
}
void
operator delete[](void* storage, std::align_val_t) noexcept
{
  allocation_tracking::free_aligned(storage);
}
void
operator delete(void* storage, std::size_t, std::align_val_t) noexcept
{
  allocation_tracking::free_aligned(storage);
}
void
operator delete[](void* storage, std::size_t, std::align_val_t) noexcept
{
  allocation_tracking::free_aligned(storage);
}
void
operator delete(void* storage, std::align_val_t, std::nothrow_t const&) noexcept
{
  allocation_tracking::free_aligned(storage);
}
void
operator delete[](void* storage, std::align_val_t, std::nothrow_t const&) noexcept
{
  allocation_tracking::free_aligned(storage);
}

namespace
{
struct options
{
  std::size_t warmup = 200;
  std::size_t samples = 2000;
  std::string output;
  bool self_test = false;
};

auto
parse_count(std::string const& value, bool allow_zero) -> std::size_t
{
  std::size_t consumed = 0;
  if (value.empty() || value.front() == '-')
    throw std::invalid_argument("sample counts must be nonnegative integers");
  auto const count = std::stoull(value, &consumed);
  if (consumed != value.size() || count > std::numeric_limits<std::size_t>::max() || (!allow_zero && count == 0))
    throw std::invalid_argument("invalid sample count");
  return static_cast<std::size_t>(count);
}

auto
parse_options(int argc, char** argv) -> options
{
  options result;
  for (int index = 1; index < argc; ++index)
    {
      std::string const argument = argv[index];
      if (argument == "--self-test")
        result.self_test = true;
      else if (argument == "--help")
        {
          std::cout << "Usage: thread_allocation_benchmarks [--output PATH] [--warmup N] [--samples N] [--self-test]\n";
          std::exit(0);
        }
      else if ((argument == "--output" || argument == "--warmup" || argument == "--samples") && index + 1 < argc)
        {
          std::string const value = argv[++index];
          if (argument == "--output")
            result.output = value;
          else if (argument == "--warmup")
            result.warmup = parse_count(value, true);
          else
            result.samples = parse_count(value, false);
        }
      else
        throw std::invalid_argument("unknown option or missing value: " + argument);
    }
  return result;
}

void
require(bool condition, char const* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

auto
count_one(thread_bench::scenario const& selected, threadschedule::thread_config const& config)
    -> allocation_tracking::counts
{
  thread_bench::startup_state state{ selected, config };
  allocation_tracking::window counting;
  thread_bench::run_thread(selected, config, thread_bench::startup_callable{ &state }, []() noexcept {});
  auto const result = counting.finish();
  if (state.failure)
    std::rethrow_exception(state.failure);
  return result;
}

// Explicit operator calls prevent new-expression allocation elision. Every
// allocation/delete overload is exercised and zero-size requests count zero
// requested bytes, even though the underlying allocator reserves storage.
void
test_allocation_forms()
{
  auto const alignment = std::align_val_t{ 64 };
  bool aligned = true;
  allocation_tracking::window counting;
  ::operator delete(::operator new(3));
  ::operator delete[](::operator new[](5));
  ::operator delete(::operator new(7), std::size_t{ 7 });
  ::operator delete[](::operator new[](11), std::size_t{ 11 });
  ::operator delete(::operator new(13, std::nothrow), std::nothrow);
  ::operator delete[](::operator new[](17, std::nothrow), std::nothrow);
  auto* storage = ::operator new(19, alignment);
  aligned = aligned && reinterpret_cast<std::uintptr_t>(storage) % 64 == 0;
  ::operator delete(storage, alignment);
  storage = ::operator new[](23, alignment);
  aligned = aligned && reinterpret_cast<std::uintptr_t>(storage) % 64 == 0;
  ::operator delete[](storage, alignment);
  storage = ::operator new(29, alignment);
  aligned = aligned && reinterpret_cast<std::uintptr_t>(storage) % 64 == 0;
  ::operator delete(storage, std::size_t{ 29 }, alignment);
  storage = ::operator new[](31, alignment);
  aligned = aligned && reinterpret_cast<std::uintptr_t>(storage) % 64 == 0;
  ::operator delete[](storage, std::size_t{ 31 }, alignment);
  storage = ::operator new(37, alignment, std::nothrow);
  aligned = aligned && storage != nullptr && reinterpret_cast<std::uintptr_t>(storage) % 64 == 0;
  ::operator delete(storage, alignment, std::nothrow);
  storage = ::operator new[](41, alignment, std::nothrow);
  aligned = aligned && storage != nullptr && reinterpret_cast<std::uintptr_t>(storage) % 64 == 0;
  ::operator delete[](storage, alignment, std::nothrow);
  storage = ::operator new(0);
  bool const zero_nonnull = storage != nullptr;
  ::operator delete(storage);
  storage = ::operator new(0, alignment);
  bool const aligned_zero_nonnull = storage != nullptr;
  ::operator delete(storage, alignment);
  storage = ::operator new(2, std::align_val_t{ 1 });
  bool const small_alignment_nonnull = storage != nullptr;
  ::operator delete(storage, std::align_val_t{ 1 });
  auto const counted = counting.finish();
  require(aligned && zero_nonnull && aligned_zero_nonnull && small_alignment_nonnull,
          "allocation alignment/zero-size self-test failed");
  require(counted.allocations == 15 && counted.bytes == 238, "allocation count/requested-byte self-test failed");
  ::operator delete(::operator new(43));
  require(allocation_tracking::allocations.load() == counted.allocations
              && allocation_tracking::bytes.load() == counted.bytes,
          "disabled counting window changed counters");
}

std::size_t handler_calls = 0;

void
failing_handler()
{
  ++handler_calls;
  throw std::bad_alloc();
}

class handler_guard
{
public:
  explicit handler_guard(std::new_handler handler) noexcept : previous_(std::set_new_handler(handler)) {}
  handler_guard(handler_guard const&) = delete;
  auto operator=(handler_guard const&) -> handler_guard& = delete;
  ~handler_guard()
  {
    std::set_new_handler(previous_);
  }

private:
  std::new_handler previous_;
};

void
test_failed_allocations()
{
  handler_guard const restore_handler(failing_handler);
  // No complete C++ object can occupy SIZE_MAX bytes. Volatile also prevents
  // compilers diagnosing this intentional failure as a constant-size mistake.
  volatile std::size_t const impossible_size = std::numeric_limits<std::size_t>::max();
  handler_calls = 0;
  allocation_tracking::window counting;
  bool threw = false;
  try
    {
      ::operator delete(::operator new(impossible_size));
    }
  catch (std::bad_alloc const&)
    {
      threw = true;
    }
  auto* const scalar = ::operator new(impossible_size, std::nothrow);
  auto* const array = ::operator new[](impossible_size, std::nothrow);
  auto* const aligned_scalar = ::operator new(impossible_size, std::align_val_t{ 64 }, std::nothrow);
  auto* const aligned_array = ::operator new[](impossible_size, std::align_val_t{ 64 }, std::nothrow);
  auto const counted = counting.finish();
  ::operator delete(scalar, std::nothrow);
  ::operator delete[](array, std::nothrow);
  ::operator delete(aligned_scalar, std::align_val_t{ 64 }, std::nothrow);
  ::operator delete[](aligned_array, std::align_val_t{ 64 }, std::nothrow);
  require(threw && scalar == nullptr && array == nullptr && aligned_scalar == nullptr && aligned_array == nullptr,
          "throwing/nothrow allocation failure self-test failed");
  require(handler_calls == 5 && counted.allocations == 0 && counted.bytes == 0,
          "new_handler/failed allocation count self-test failed");
}

void
self_test()
{
  test_allocation_forms();
  test_failed_allocations();
  threadschedule::thread_config const config;
  auto const counted = count_one(thread_bench::scenarios.front(), config);
  require(counted.allocations > 0 && counted.bytes > 0, "thread allocation self-test failed");
  std::cout << "Allocation benchmark self-tests passed.\n";
}

struct measurements
{
  std::vector<double> allocations;
  std::vector<double> bytes;
  std::string error;
};

auto
collect(options const& settings) -> nlohmann::json
{
  threadschedule::thread_config config;
  config.set_name("ts-bench");
  std::array<measurements, thread_bench::scenarios.size()> results;
  for (auto& result : results)
    {
      result.allocations.reserve(settings.samples);
      result.bytes.reserve(settings.samples);
    }

  auto const run_rounds = [&](std::size_t rounds, bool measured)
    {
      for (std::size_t round = 0; round < rounds; ++round)
        for (std::size_t offset = 0; offset < results.size(); ++offset)
          {
            auto const index = (round % results.size() + offset) % results.size();
            auto& result = results[index];
            if (!result.error.empty())
              continue;
            try
              {
                auto const counted = count_one(thread_bench::scenarios[index], config);
                if (measured)
                  {
                    result.allocations.push_back(static_cast<double>(counted.allocations));
                    result.bytes.push_back(static_cast<double>(counted.bytes));
                  }
              }
            catch (std::exception const& error)
              {
                result.error = error.what();
              }
          }
    };
  run_rounds(settings.warmup, false);
  run_rounds(settings.samples, true);

  nlohmann::json report = { { "schema_version", 1 },
                            { "kind", "allocations" },
                            { "metadata", thread_bench::metadata() },
                            { "parameters", { { "warmup", settings.warmup }, { "samples", settings.samples } } },
                            { "sizes", thread_bench::sizes() },
                            { "scenarios", nlohmann::json::array() } };
  for (std::size_t index = 0; index < results.size(); ++index)
    {
      auto const& selected = thread_bench::scenarios[index];
      auto const& result = results[index];
      nlohmann::json row = { { "id", selected.id },
                             { "label", selected.label },
                             { "configured", selected.configured },
                             { "status", result.error.empty() ? "ok" : "error" } };
      if (!result.error.empty())
        row["error"] = result.error;
      else
        {
          row["metrics"] = { { "allocations", thread_bench::summary(result.allocations) },
                             { "allocated_bytes", thread_bench::summary(result.bytes) } };
          row["samples"] = { { "allocations", result.allocations }, { "allocated_bytes", result.bytes } };
        }
      report["scenarios"].push_back(std::move(row));
    }
  return report;
}

auto
print_report(nlohmann::json const& report) -> bool
{
  bool success = true;
  std::cout << "C++ allocation requests per creation/join cycle (median / p95)\n"
            << "Excludes OS stacks, direct malloc and other C-library/OS allocations.\n"
            << std::left << std::setw(34) << "Scenario" << std::right << std::setw(22) << "Allocations" << std::setw(22)
            << "Requested bytes" << '\n';
  for (auto const& row : report["scenarios"])
    {
      std::cout << std::left << std::setw(34) << row["label"].get<std::string>();
      if (row["status"] != "ok")
        {
          std::cout << "ERROR: " << row["error"].get<std::string>() << '\n';
          success = false;
          continue;
        }
      for (auto const* metric : { "allocations", "allocated_bytes" })
        {
          auto const& value = row["metrics"][metric];
          std::cout << std::right << std::setw(9) << value["median"].get<double>() << " / " << std::setw(10)
                    << value["p95"].get<double>();
        }
      std::cout << '\n';
    }
  return success;
}
} // namespace

auto
main(int argc, char** argv) -> int
{
  try
    {
      auto const settings = parse_options(argc, argv);
      if (settings.self_test)
        {
          self_test();
          return 0;
        }
      auto const report = collect(settings);
      auto const success = print_report(report);
      if (!settings.output.empty())
        thread_bench::write_json(settings.output, report);
      return success ? 0 : 1;
    }
  catch (std::exception const& error)
    {
      std::cerr << "Allocation benchmark failed: " << error.what() << '\n';
      return 1;
    }
}
