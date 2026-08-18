#pragma once

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>

namespace threadschedule::detail
{

class unique_handle
{
public:
  unique_handle() noexcept = default;
  explicit unique_handle(HANDLE handle) noexcept : handle_(handle) {}

  unique_handle(unique_handle const&) = delete;
  auto operator=(unique_handle const&) -> unique_handle& = delete;

  unique_handle(unique_handle&& other) noexcept : handle_(other.release()) {}

  auto
  operator=(unique_handle&& other) noexcept -> unique_handle&
  {
    if (this != &other)
      reset(other.release());
    return *this;
  }

  ~unique_handle()
  {
    reset();
  }

  [[nodiscard]] auto
  get() const noexcept -> HANDLE
  {
    return handle_;
  }

  [[nodiscard]] explicit
  operator bool() const noexcept
  {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
  }

  [[nodiscard]] auto
  release() noexcept -> HANDLE
  {
    auto* value = handle_;
    handle_ = nullptr;
    return value;
  }

  void
  reset(HANDLE handle = nullptr) noexcept
  {
    if (handle_ == handle)
      return;
    if (*this)
      CloseHandle(handle_);
    handle_ = handle;
  }

private:
  HANDLE handle_{ nullptr };
};

} // namespace threadschedule::detail
#endif
