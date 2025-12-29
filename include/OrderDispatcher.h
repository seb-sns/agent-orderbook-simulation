#pragma once

#include "MatchingEngine.h"
#include "RingBuffer.h"
#include <atomic>
#include <cstdint>
#include <vector>

class OrderDispatcher {
public:
  OrderDispatcher(MatchingEngine &matchingEngine)
      : matchingEngine_(matchingEngine) {};

  void Attach(RingBuffer<Order, 1024> *buffer);
  void Start();
  void Stop();

  std::uint64_t GetPushedOrders() const { return pushedOrders_; }

private:
  std::uint64_t pushedOrders_{0};
  MatchingEngine &matchingEngine_;
  std::vector<RingBuffer<Order, 1024> *> userBuffers_;
  std::atomic<bool> running_{false};
  std::uint64_t counter_{1};
};
