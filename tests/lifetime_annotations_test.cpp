#include <threadschedule/detail/callable/function_ref.hpp>
#include <threadschedule/expected.hpp>
#include <threadschedule/thread_affinity.hpp>
#include <threadschedule/thread_config.hpp>
#include <threadschedule/thread_view.hpp>

#include <memory>
#include <utility>

namespace threadschedule::tests
{

using int_result = expected<int, int>;
using void_result = expected<void, int>;

// A consumer-defined view deliberately has no lifetimebound constructor. The
// Owner/Pointer relationship alone must diagnose a temporary affinity owner.
struct THREADSCHEDULE_POINTER affinity_borrow
{
  explicit affinity_borrow(thread_affinity const& owner) : source(&owner) {}
  thread_affinity const* source;
};

#if defined(THREADSCHEDULE_TEST_DANGLING)

// Compiled only by Clang's diagnostic verifier, never executed. Each warning
// must appear at the public call site; removing an annotation must fail this test.
void
temporary_owners()
{
  auto const& value = int_result(42).value();                // expected-warning {{temporary bound to local reference}}
  auto const& error = int_result(unexpect, 7).error();       // expected-warning {{temporary bound to local reference}}
  auto const& dereferenced = *int_result(42);                // expected-warning {{temporary bound to local reference}}
  auto* pointer = int_result(42).operator->();               // expected-warning {{temporary whose address is used}}
  auto const& void_error = void_result(unexpect, 7).error(); // expected-warning {{temporary bound to local reference}}
  auto const& unexpected_error = unexpected<int>(7).error(); // expected-warning {{temporary bound to local reference}}
  auto const& exception_error
      = bad_expected_access<int>(7).error();                 // expected-warning {{temporary bound to local reference}}
  auto const& name = thread_config{}.get_name();             // expected-warning {{temporary bound to local reference}}
  auto const& scheduling = thread_config{}.get_scheduling(); // expected-warning {{temporary bound to local reference}}
  auto const& affinity = thread_config{}.get_affinity();     // expected-warning {{temporary bound to local reference}}
  auto const& cpus = thread_affinity{}.cpus();               // expected-warning {{temporary bound to local reference}}
  (void)value;
  (void)error;
  (void)dereferenced;
  (void)pointer;
  (void)void_error;
  (void)unexpected_error;
  (void)exception_error;
  (void)name;
  (void)scheduling;
  (void)affinity;
  (void)cpus;
}

auto
lvalue_value() -> int&
{
  int_result owner(42);
  return owner.value(); // expected-warning {{reference to stack memory}}
}

auto
const_lvalue_value() -> int const&
{
  int_result const owner(42);
  return owner.value(); // expected-warning {{reference to stack memory}}
}

auto
const_rvalue_value() -> int const&&
{
  int_result const owner(42);
  return std::move(owner).value(); // expected-warning {{reference to stack memory}}
}

auto
lvalue_error() -> int&
{
  int_result owner(unexpect, 7);
  return owner.error(); // expected-warning {{reference to stack memory}}
}

auto
const_lvalue_error() -> int const&
{
  int_result const owner(unexpect, 7);
  return owner.error(); // expected-warning {{reference to stack memory}}
}

auto
const_rvalue_error() -> int const&&
{
  int_result const owner(unexpect, 7);
  return std::move(owner).error(); // expected-warning {{reference to stack memory}}
}

auto
lvalue_dereference() -> int&
{
  int_result owner(42);
  return *owner; // expected-warning {{reference to stack memory}}
}

auto
const_lvalue_dereference() -> int const&
{
  int_result const owner(42);
  return *owner; // expected-warning {{reference to stack memory}}
}

auto
const_rvalue_dereference() -> int const&&
{
  int_result const owner(42);
  return *std::move(owner); // expected-warning {{reference to stack memory}}
}

auto
const_pointer() -> int const*
{
  int_result const owner(42);
  return owner.operator->(); // expected-warning {{address of stack memory}}
}

auto
expired_standard_thread() -> thread_view
{
  std::thread owner;
  return thread_view(owner); // expected-warning {{address of stack memory}}
}

auto
expired_thread() -> thread_view
{
  thread owner;
  return thread_view(owner); // expected-warning {{address of stack memory}}
}

#  if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
auto
expired_standard_jthread() -> thread_view
{
  std::jthread owner;
  return thread_view(owner); // expected-warning {{address of stack memory}}
}

auto
expired_jthread() -> thread_view
{
  jthread owner;
  return thread_view(owner); // expected-warning {{address of stack memory}}
}
#  endif

void
expired_callable()
{
  detail::function_ref<int()> callable = [value = 42] { return value; }; // expected-warning {{temporary whose address}}
  (void)callable;
}

void
expired_affinity()
{
  auto borrowed = affinity_borrow(thread_affinity{}); // expected-warning {{object backing the pointer}}
  (void)borrowed;
}

#else

// These cases must remain warning-free, including moves out of temporaries.
void
valid_borrows()
{
  int_result owner(42);
  auto& value = owner.value();
  auto const& const_value = std::as_const(owner).value();
  auto& dereferenced = *owner;
  auto const* pointer = std::as_const(owner).operator->();
  value = const_value + dereferenced + *pointer;

  int_result failed(unexpect, 7);
  auto& error = failed.error();
  error = std::as_const(failed).error();
  void_result void_failed(unexpect, 7);
  auto& void_error = void_failed.error();
  void_error = std::as_const(void_failed).error();

  thread_config config;
  auto const& name = config.get_name();
  auto const& scheduling = config.get_scheduling();
  auto const& affinity = config.get_affinity();
  thread_affinity mask;
  auto const& cpus = mask.cpus();
  (void)name;
  (void)scheduling;
  (void)affinity;
  (void)cpus;
}

void
owned_results()
{
  auto value = int_result(42).value();
  auto error = int_result(unexpect, 7).error();
  auto dereferenced = *int_result(42);
  auto void_error = void_result(unexpect, 7).error();
  auto name = thread_config{}.get_name();
  auto cpus = thread_affinity{}.cpus();
  auto moved = expected<std::unique_ptr<int>>(std::make_unique<int>(42)).value();
  (void)value;
  (void)error;
  (void)dereferenced;
  (void)void_error;
  (void)name;
  (void)cpus;
  (void)moved;
}

auto
function_target() -> int
{
  return 42;
}

auto
function_pointer_view() -> detail::function_ref<int()>
{
  // The function pointer is copied, so its local variable need not survive.
  auto pointer = &function_target;
  return detail::function_ref<int()>(pointer);
}

void
valid_views()
{
  auto callable = [value = std::make_unique<int>(42)] { return *value; };
  detail::function_ref<int()> borrowed_callable(callable);
  auto copied_callable = borrowed_callable;
  (void)copied_callable();
  (void)function_pointer_view()();

  std::thread standard;
  thread owned;
  thread_view standard_view(standard);
  thread_view owned_view(owned);
  auto copied_view = standard_view;
  (void)copied_view.joinable();
  (void)owned_view.joinable();
#  if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
  std::jthread standard_jthread;
  jthread owned_jthread;
  thread_view standard_jthread_view(standard_jthread);
  thread_view owned_jthread_view(owned_jthread);
  (void)standard_jthread_view.joinable();
  (void)owned_jthread_view.joinable();
#  endif

  thread_affinity affinity;
  affinity_borrow borrowed_affinity(affinity);
  (void)borrowed_affinity.source->empty();

  // All three arguments may be temporary: the operations retain no borrow.
  (void)standard_view.set_name(std::string("worker"));
  (void)standard_view.set_affinity(thread_affinity{});
  (void)standard_view.configure(thread_config{});
}

#endif

} // namespace threadschedule::tests
