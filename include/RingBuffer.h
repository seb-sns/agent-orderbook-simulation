#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <iostream>

template <typename T>
concept viewable = requires(std::ostream &os, T val) {
  { os << val } -> std::same_as<std::ostream &>;
};

template <typename T, size_t n> class RingBuffer {
  static_assert((n & (n - 1)) == 0, "Size is not a power of 2");

private:
  alignas(64) std::array<T, n> buffer_;
  alignas(64) std::atomic<size_t> tail_{0};
  alignas(64) std::atomic<size_t> head_{0};

public:
  bool Push(T &&item);
  bool Pop(T &item);
  size_t size() const;
  bool empty() const;
  bool full() const;
  void Print() const
    requires viewable<T>;
};

template <typename T, std::size_t n> bool RingBuffer<T, n>::Push(T &&item) {
  size_t current_head = head_.load(std::memory_order_relaxed);
  size_t next_head = (current_head + 1) & (n - 1);
  if (next_head == tail_.load(std::memory_order_acquire)) {
    return false;
  }

  buffer_[current_head] = std::move(item);
  head_.store(next_head, std::memory_order_release);
  return true;
}

template <typename T, std::size_t n> bool RingBuffer<T, n>::Pop(T &item) {
  size_t current_tail = tail_.load(std::memory_order_relaxed);

  if (current_tail == head_.load(std::memory_order_acquire)) {
    return false;
  }

  item = std::move(buffer_[current_tail]);
  size_t next_tail = (current_tail + 1) & (n - 1);
  tail_.store(next_tail, std::memory_order_release);
  return true;
}

template <typename T, std::size_t n> size_t RingBuffer<T, n>::size() const {
  size_t current_head = head_.load(std::memory_order_acquire);
  size_t current_tail = tail_.load(std::memory_order_acquire);

  return (current_head - current_tail) & (n - 1);
}

template <typename T, std::size_t n> bool RingBuffer<T, n>::empty() const {
  size_t current_head = head_.load(std::memory_order_acquire);
  size_t current_tail = tail_.load(std::memory_order_acquire);

  return current_head == current_tail;
}

template <typename T, std::size_t n> bool RingBuffer<T, n>::full() const {
  size_t current_tail = tail_.load(std::memory_order_acquire);
  size_t current_head = head_.load(std::memory_order_acquire);
  return ((current_head + 1) & (n - 1)) == current_tail;
}

template <typename T, size_t n>
void RingBuffer<T, n>::Print() const
  requires viewable<T>
{
  size_t current_size = size();
  if (current_size == 0) {
    std::cout << "Buffer is empty." << std::endl;
    return;
  }

  size_t current_head = head_.load(std::memory_order_acquire);
  size_t current_tail = tail_.load(std::memory_order_acquire);

  std::cout << "Buffer contents (" << current_size << "/" << n << "): ";
  for (size_t i = current_tail; i < current_head; ++i) {
    std::cout << "(i: " << i << ") ";
    std::cout << buffer_[i];
    if (i < current_head - 1) {
      std::cout << ", ";
    }
  }
  std::cout << std::endl;
}
