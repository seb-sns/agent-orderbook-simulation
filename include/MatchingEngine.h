#pragma once

#include "AgentStrategy.h"
#include "OrderPool.h"
#include "Orderbook.h"
#include "RingBuffer.h"
#include <cstdint>
#include <unordered_map>

class Dispatcher;
class AgentManager;

class MatchingEngine {
public:
  void ProcessOrder(Order *order);
  MatchingEngine(Orderbook &orderbook, OrderPool *orderPool)
      : orderbook_(orderbook), orderPool_(orderPool) {};

  void Start();
  void Stop();

  std::uint64_t GetProcessedOrders() const { return ordersProcessed_; }

  friend class OrderDispatcher;
  friend class Agent;
  friend class AgentManager;

private:
  std::uint64_t counter_{0};
  std::uint64_t ordersProcessed_{0}; // for benchmarking
  Orderbook &orderbook_;
  OrderPool *orderPool_;
  RingBuffer<Order *, 1024> orders_;
  std::unordered_map<OrderId, Order *> ordersMap_;

  std::atomic<bool> running_{false};

  void MatchLimitOrder(Order *order);
  void MatchMarketOrder(Order *order);
  void CancelOrder(Order *order);

  Order *CreateCancelOrder(Order *order);
};
