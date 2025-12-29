#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

template <typename T, std::size_t n, typename TimeAccessor>
class CalenderQueue {
  static_assert((n & (n - 1)) == 0, "Size is not a power of 2");

private:
  using Bucket = std::vector<T>;
  std::array<Bucket, n> buckets_{};
  std::size_t bucket_width_{1};
  TimeAccessor time_accessor_{};
  std::size_t head_{0};
  std::size_t size_{0};

public:
  bool Push(T &&item);
  bool Pop(T &item);
  // size_t size() const;
  bool empty() const;
  // bool full() const;
};

template <typename T, std::size_t n, typename TimeAccessor>
bool CalenderQueue<T, n, TimeAccessor>::Push(T &&item) {

  std::size_t timestamp = static_cast<std::size_t>(time_accessor_(item));
  std::size_t bucketIndex = (timestamp / bucket_width_) & (n - 1);

  auto &bucket = buckets_[bucketIndex];
  auto iter = std::upper_bound(bucket.begin(), bucket.end(), item,
                               [&](const T &a, const T &b) {
                               return time_accessor_(a) > time_accessor_(b);
                              });
  bucket.insert(iter, std::move(item));
  ++size_;
  return true;
};

template <typename T, std::size_t n, typename TimeAccessor>
bool CalenderQueue<T, n, TimeAccessor>::Pop(T &item) {
  if (size_ == 0) {
    return false;
  }

  while (buckets_[head_].empty()) {
    head_ = (head_ + 1) & (n - 1);
  }
  auto &bucket= buckets_[head_];
  item = std::move(bucket.back());
  bucket.pop_back();
  --size_;
  return true;
}

template <typename T, std::size_t n, typename TimeAccessor>
bool CalenderQueue<T, n, TimeAccessor>::empty() const { return size_ == 0; }
