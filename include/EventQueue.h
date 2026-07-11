#pragma once

#include <cstddef>
#include <utility>
#include <vector>

// Min-heap on Event::time for the agent event loop. The loop's access
// pattern is always "pop the earliest, act, push that agent's next event",
// so ReplaceTop (one sift-down) does the work of a pop+push pair — ~30%
// faster than std::priority_queue at typical agent counts (see
// benchmarks/benchmark_EventQueue.cpp; chosen over 4-ary heap, flat argmin
// and a calendar queue, all of which lose either at small or large n).
template <typename Event> class EventQueue {
public:
  void Reserve(std::size_t n) { heap_.reserve(n); }
  bool Empty() const { return heap_.empty(); }
  std::size_t Size() const { return heap_.size(); }
  const Event &Top() const { return heap_.front(); }

  void Push(const Event &event) {
    heap_.push_back(event);
    std::size_t i = heap_.size() - 1;
    while (i > 0 && heap_[(i - 1) / 2].time > heap_[i].time) {
      std::swap(heap_[(i - 1) / 2], heap_[i]);
      i = (i - 1) / 2;
    }
  }

  // Replace the earliest event and restore the heap with a single sift-down.
  void ReplaceTop(const Event &event) {
    heap_[0] = event;
    std::size_t i = 0;
    const std::size_t n = heap_.size();
    while (true) {
      const std::size_t left = 2 * i + 1;
      const std::size_t right = left + 1;
      std::size_t smallest = i;
      if (left < n && heap_[left].time < heap_[smallest].time) {
        smallest = left;
      }
      if (right < n && heap_[right].time < heap_[smallest].time) {
        smallest = right;
      }
      if (smallest == i) {
        return;
      }
      std::swap(heap_[i], heap_[smallest]);
      i = smallest;
    }
  }

private:
  std::vector<Event> heap_;
};
