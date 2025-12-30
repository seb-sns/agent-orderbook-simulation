#pragma once

#include <array>
#include <cstddef>
#include <cstdio>

template <typename T, size_t n> class SingleThreadRingBuffer {
  static_assert((n & (n - 1)) == 0, "Size is not a power of 2");

private:
  std::array<T, n> buffer_;
  size_t tail_{0};
  size_t head_{0};

public:
  bool Push(T &&item);
  bool Push(const T &item);
  bool Pop(T &item);
  size_t size() const;
  bool empty() const;
  bool full() const;
};

template <typename T, std::size_t n>
bool SingleThreadRingBuffer<T, n>::Push(T &&item) {
  size_t next_head = (head_ + 1) & (n - 1);
  if (next_head == tail_) {
    return false;
  }

  buffer_[head_] = std::move(item);
  head_ = next_head;
  return true;
}

template <typename T, std::size_t n>
bool SingleThreadRingBuffer<T, n>::Push(const T &item) {
  size_t next_head = (head_ + 1) & (n - 1);
  if (next_head == tail_) {
    return false;
  }

  buffer_[head_] = item;
  head_ = next_head;
  return true;
}

template <typename T, std::size_t n>
bool SingleThreadRingBuffer<T, n>::Pop(T &item) {

  if (tail_ == head_) {
    return false;
  }

  item = std::move(buffer_[tail_]);
  size_t next_tail = (tail_ + 1) & (n - 1);
  tail_ = next_tail;
  return true;
}

template <typename T, std::size_t n>
size_t SingleThreadRingBuffer<T, n>::size() const {
  return (head_ - tail_) & (n - 1);
}

template <typename T, std::size_t n>
bool SingleThreadRingBuffer<T, n>::empty() const {
  return head_ == tail_;
}

template <typename T, std::size_t n>
bool SingleThreadRingBuffer<T, n>::full() const {
  return ((head_ + 1) & (n - 1)) == tail_;
}
