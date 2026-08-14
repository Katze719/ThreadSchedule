#pragma once

/** @file detail/pool/work_stealing_deque.hpp
 *  @brief Bounded owner/thief queue used by the work-stealing pool.
 */

template <typename T>
class work_stealing_deque
{
public:
  static constexpr size_t cache_line_size = 64;
  static constexpr size_t default_capacity = 1024;

private:
  struct alignas(cache_line_size) aligned_item
  {
    T item;
    aligned_item() = default;
    aligned_item(T&& value) : item(std::move(value)) {}
    template <typename U = T, std::enable_if_t<std::is_copy_constructible_v<U>, int> = 0>
    aligned_item(T const& value) : item(value)
    {
    }
  };

  std::unique_ptr<aligned_item[]> buffer_;
  size_t capacity_;
  alignas(cache_line_size) std::atomic<size_t> top_{ 0 };
  alignas(cache_line_size) std::atomic<size_t> bottom_{ 0 };
  alignas(cache_line_size) mutable std::mutex mutex_;

public:
  explicit work_stealing_deque(size_t capacity = default_capacity)
      : buffer_(std::make_unique<aligned_item[]>(capacity)), capacity_(capacity)
  {
  }

  [[nodiscard]] auto
  push(T&& item) -> bool
  {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t const top = top_.load(std::memory_order_relaxed);
    size_t const bottom = bottom_.load(std::memory_order_relaxed);
    if (top - bottom >= capacity_)
      return false;
    buffer_[top % capacity_] = aligned_item(std::move(item));
    top_.store(top + 1, std::memory_order_release);
    return true;
  }

  template <typename U = T, std::enable_if_t<std::is_copy_constructible_v<U>, int> = 0>
  [[nodiscard]] auto
  push(T const& item) -> bool
  {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t const top = top_.load(std::memory_order_relaxed);
    size_t const bottom = bottom_.load(std::memory_order_relaxed);
    if (top - bottom >= capacity_)
      return false;
    buffer_[top % capacity_] = aligned_item(item);
    top_.store(top + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] auto
  pop(T& item) -> bool
  {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t const top = top_.load(std::memory_order_relaxed);
    size_t const bottom = bottom_.load(std::memory_order_relaxed);
    if (top <= bottom)
      return false;
    size_t const new_top = top - 1;
    item = std::move(buffer_[new_top % capacity_].item);
    top_.store(new_top, std::memory_order_relaxed);
    return true;
  }

  [[nodiscard]] auto
  steal(T& item) -> bool
  {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t const bottom = bottom_.load(std::memory_order_relaxed);
    size_t const top = top_.load(std::memory_order_relaxed);
    if (bottom >= top)
      return false;
    item = std::move(buffer_[bottom % capacity_].item);
    bottom_.store(bottom + 1, std::memory_order_relaxed);
    return true;
  }

  [[nodiscard]] auto
  size() const -> size_t
  {
    size_t const top = top_.load(std::memory_order_relaxed);
    size_t const bottom = bottom_.load(std::memory_order_relaxed);
    return top > bottom ? top - bottom : 0;
  }

  [[nodiscard]] auto
  empty() const -> bool
  {
    return size() == 0;
  }

  void
  clear()
  {
    (void)clear_and_count();
  }

  [[nodiscard]] auto
  clear_and_count() -> size_t
  {
    size_t count = 0;
    T discarded;
    while (steal(discarded))
      {
        ++count;
        discarded = T{};
      }
    return count;
  }
};
