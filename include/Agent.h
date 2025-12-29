#pragma once
#include "AgentStrategy.h"
#include "MatchingEngine.h"
#include "Order.h"
#include "OrderPool.h"
#include "RingBuffer.h"
#include "Trade.h"
#include "TradeDispatcher.h"
#include <atomic>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>

struct AgentInfo {
  enum class Strategy { RANDOM, MARKETMAKER, MOMENTUMTRADER };
  ClientRef clientRef_;
  Strategy strategy_;
  std::int64_t cash_;
  std::int64_t units_;
};

class Agent {
public:
  Agent(TradeDispatcher &tradeDispatcher, MatchingEngine &matchingEngine,
        AgentStrategy strategy, ClientRef clientRef, double rate);
  ~Agent();

  OrderPtrs Act();
  void PushOrder(Order *order);
  void PushTrade(TradeInfo &&tradeInfo);
  void PopTrade();
  void AddActiveOrder(PoolIndex index, Order *order);
  void RemoveActiveOrder(PoolIndex index);
  double ScheduleNextAction(std::uint64_t currentTime);

  std::int64_t GetCash() const { return cash_; }
  std::int64_t GetAvailableCash() const { return availableCash_; }
  std::int64_t GetReservedCash() const { return reservedCash_; }
  std::int64_t GetUnits() const { return units_; }
  ClientRef GetClientRef() const { return clientRef_; }
  std::unordered_map<PoolIndex, Order *> GetActiveOrders() const;
  std::map<Price, std::unordered_set<PoolIndex>> GetActiveOrdersByPrice() const;
  AgentInfo GetInfo();

  void ClearIncoming();
  void PrintState();

private:
  void PushLimitOrder(Order *order);
  void PushMarketOrder(Order *order);
  void PushCancelOrder(Order *order);

  void PopLimitOrderTrade(TradeInfo &tradeInfo);
  void PopMarketOrderTrade(TradeInfo &tradeInfo);
  void PopCancelOrderTrade(TradeInfo &tradeInfo);

private:
  AgentStrategy strategy_;
  MatchingEngine &matchingEngine_;
  TradeDispatcher &tradeDispatcher_;
  RingBuffer<TradeInfo, 1024> incomingBuffer_;
  std::unordered_map<PoolIndex, Order *> activeOrders_;
  std::map<Price, std::unordered_set<PoolIndex>> activeOrdersByPrice_;
  OrderId agentOrders_ = 0;
  ClientRef clientRef_;
  std::int64_t initialCash_{1'000'000'000};
  std::int64_t initialUnits_{100'000};
  std::atomic<std::int64_t> cash_{1'000'000'000};
  std::atomic<std::int64_t> reservedCash_{0};
  std::atomic<std::int64_t> availableCash_{1'000'000'000};
  std::atomic<std::int64_t> units_{100'000};
  double rate_;
  mutable std::shared_mutex mtx_;
};
