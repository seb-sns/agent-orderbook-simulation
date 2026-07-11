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
#include <mutex>
#include <unordered_map>
#include <vector>

struct AgentEvent {
  double time;
  size_t pos;

  bool operator<(const AgentEvent &otherEvent) const {
    return time > otherEvent.time;
  }
};

struct AgentInfo {
  enum class Strategy { RANDOM, MARKETMAKER, MOMENTUMTRADER, MEANREVERTER, WHALE };
  ClientRef clientRef_;
  Strategy strategy_;
  std::int64_t cash_;
  std::int64_t units_;
};

// A by-value record of one of the agent's live orders, owned exclusively by
// the outgoing (strategy) thread. id stays 0 until the engine's ACK arrives;
// strategies must not cancel a record whose id is still 0.
struct ActiveOrder {
  OrderId id{0};
  Price price{0};
  Side side{Side::Buy};
};

// Threading model:
//  - outgoing thread: Act(), PushOrder(), DrainOrderEvents(), activeOrders_
//  - matching thread: PushTrade() (via TradeDispatcher)
//  - incoming thread: PopTrade(), which updates cash/units (atomics) and
//    forwards order lifecycle changes to the outgoing thread via orderEvents_
// No pool pointers are shared across threads: cancels are built from the
// by-value ActiveOrder records using engine-echoed order ids.
class Agent {
public:
  Agent(TradeDispatcher &tradeDispatcher, MatchingEngine &matchingEngine,
        AgentStrategy &&strategy, ClientRef clientRef,
        double meanActionInterval);
  ~Agent();

  OrderPtrs Act();
  void PushOrder(Order *order);
  void PushTrade(TradeInfo &&tradeInfo);
  bool PopTrade();
  void DrainOrderEvents();
  double ScheduleNextAction(double currentTime);

  std::int64_t GetCash() const { return cash_; }
  std::int64_t GetAvailableCash() const { return availableCash_; }
  std::int64_t GetReservedCash() const { return reservedCash_; }
  std::int64_t GetUnits() const { return units_; }
  std::int64_t GetInitialUnits() const { return initialUnits_; }
  ClientRef GetClientRef() const { return clientRef_; }

  // Outgoing thread only.
  template <typename F> void ForEachActiveOrder(F &&f) {
    DrainOrderEvents();
    for (auto &[seq, record] : activeOrders_)
      f(record);
  }
  AgentInfo GetInfo();

  void ClearIncoming();
  void PrintState();

private:
  struct OrderEvent {
    std::uint64_t seq{0};
    OrderId id{0};
    bool retire{false}; // true → erase the record, false → ACK sets its id
  };

  void PushLimitOrder(Order *order);
  void PushMarketOrder(Order *order);
  void PushCancelOrder(Order *order);

  void PopLimitOrderTrade(TradeInfo &tradeInfo);
  void PopMarketOrderTrade(TradeInfo &tradeInfo);
  void PopCancelOrderTrade(TradeInfo &tradeInfo);

  void TrackOrder(Order *order);           // outgoing: stamp seq, add record
  void EnqueueToEngine(Order *order);      // outgoing: never deadlocks
  void PushOrderEvent(OrderEvent &&event); // incoming: never blocks

private:
  AgentStrategy strategy_;
  MatchingEngine &matchingEngine_;
  TradeDispatcher &tradeDispatcher_;
  RingBuffer<TradeInfo, 1024> incomingBuffer_;
  RingBuffer<OrderEvent, 4096> orderEvents_;
  std::vector<OrderEvent> orderEventOverflow_; // guarded by overflowMtx_
  std::mutex overflowMtx_;
  std::atomic<bool> hasOverflow_{false};
  std::unordered_map<std::uint64_t, ActiveOrder> activeOrders_;
  std::uint64_t nextSeq_{1};
  ClientRef clientRef_;
  std::int64_t initialCash_{1'000'000'000};
  std::int64_t initialUnits_{100'000};
  std::atomic<std::int64_t> cash_{1'000'000'000};
  std::atomic<std::int64_t> reservedCash_{0};
  std::atomic<std::int64_t> availableCash_{1'000'000'000};
  std::atomic<std::int64_t> units_{100'000};
  double meanActionInterval_;
};
