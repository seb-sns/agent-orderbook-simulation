#include "Agent.h"
#include "AgentStrategy.h"
#include "MatchingEngine.h"
#include "Order.h"
#include "OrderPool.h"
#include "Trade.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <variant>
#include <x86intrin.h>

static std::mt19937 gen(42);
static std::uniform_real_distribution<> dis(0.0, 1.0);

// meanInterval is the average number of time units between actions, matching
// the "time units/action" wording of the simulation prompts.
double sampleExponential(double meanInterval) {
  double U = dis(gen);
  return -std::log(U) * meanInterval;
}

Agent::Agent(TradeDispatcher &tradeDispatcher, MatchingEngine &matchingEngine,
             AgentStrategy &&strategy, ClientRef clientRef,
             double meanActionInterval)
    : strategy_(std::move(strategy)), clientRef_(clientRef),
      meanActionInterval_(meanActionInterval),
      matchingEngine_(matchingEngine), tradeDispatcher_(tradeDispatcher) {
  activeOrders_.reserve(512); // avoid rehashing on the outgoing hot path
  tradeDispatcher_.Attach(this);
};

Agent::~Agent() {
  ClearIncoming();
  tradeDispatcher_.Detach(this);
}

OrderPtrs Agent::Act() {
  // Apply pending ACK/retire events first so strategies see current records
  // (and so records of strategies that never cancel still get retired).
  DrainOrderEvents();
  return std::visit(
      [&](auto &activeStrategy) { return activeStrategy.Act(this); },
      strategy_);
}

double Agent::ScheduleNextAction(double currentTime) {
  return currentTime + sampleExponential(meanActionInterval_);
}

void Agent::PushOrder(Order *order) {
  if (order->GetOrderType() == OrderType::CANCEL) {
    PushCancelOrder(std::move(order));
  } else if (order->GetOrderType() == OrderType::MARKET) {
    PushMarketOrder(std::move(order));
  } else {
    PushLimitOrder(std::move(order));
  }
}

void Agent::TrackOrder(Order *order) {
  order->SetTimestamp(nextSeq_);
  activeOrders_.emplace(
      nextSeq_, ActiveOrder{0, order->GetPrice(), order->GetSide()});
  ++nextSeq_;
}

void Agent::EnqueueToEngine(Order *order) {
  while (!matchingEngine_.orders_.Push(order)) {
    // Backpressure: keep consuming our own order events so the incoming
    // thread (and transitively the engine) can always make progress.
    DrainOrderEvents();
    _mm_pause();
  }
}

void Agent::PushLimitOrder(Order *order) {
  TrackOrder(order);
  if (order->GetSide() == Side::Sell) {
    units_.fetch_sub(order->GetRemainingQuantity(), std::memory_order_relaxed);
  } else {
    int64_t priceCents = static_cast<std::int64_t>(order->GetPrice() * 100);
    int64_t totalCents =
        static_cast<std::int64_t>(priceCents * order->GetRemainingQuantity());
    availableCash_.fetch_sub(totalCents, std::memory_order_relaxed);
    reservedCash_.fetch_add(totalCents, std::memory_order_relaxed);
  }
  EnqueueToEngine(order);
}

void Agent::PushMarketOrder(Order *order) {
  TrackOrder(order);
  if (order->GetSide() == Side::Sell) {
    units_.fetch_sub(order->GetRemainingQuantity(), std::memory_order_relaxed);
  } else {
    int64_t priceCents = static_cast<std::int64_t>(order->GetPrice() * 100);
    int64_t totalCents =
        static_cast<std::int64_t>(priceCents * order->GetRemainingQuantity());
    availableCash_.fetch_sub(totalCents, std::memory_order_relaxed);
    reservedCash_.fetch_add(totalCents, std::memory_order_relaxed);
  }
  EnqueueToEngine(order);
}

void Agent::PushCancelOrder(Order *order) { EnqueueToEngine(order); }

// Matching engine thread (via TradeDispatcher).
void Agent::PushTrade(TradeInfo &&tradeInfo) {
  while (!incomingBuffer_.Push(std::move(tradeInfo))) {
    _mm_pause();
  }
}

// Incoming thread. Never blocks: order events that don't fit the ring spill
// into a mutex-guarded overflow the outgoing thread collects on drain.
void Agent::PushOrderEvent(OrderEvent &&event) {
  if (orderEvents_.Push(event)) {
    return;
  }
  std::lock_guard<std::mutex> lock(overflowMtx_);
  orderEventOverflow_.push_back(event);
  hasOverflow_.store(true, std::memory_order_release);
}

// Outgoing thread.
void Agent::DrainOrderEvents() {
  auto apply = [&](const OrderEvent &event) {
    if (event.retire) {
      activeOrders_.erase(event.seq);
    } else {
      auto it = activeOrders_.find(event.seq);
      if (it != activeOrders_.end()) {
        it->second.id = event.id;
      }
    }
  };
  OrderEvent event;
  while (orderEvents_.Pop(event)) {
    apply(event);
  }
  if (hasOverflow_.load(std::memory_order_acquire)) {
    std::vector<OrderEvent> spilled;
    {
      std::lock_guard<std::mutex> lock(overflowMtx_);
      spilled.swap(orderEventOverflow_);
      hasOverflow_.store(false, std::memory_order_release);
    }
    for (const OrderEvent &spilledEvent : spilled) {
      apply(spilledEvent);
    }
    // Ring entries pushed after the spill still drain on the next call.
  }
}

bool Agent::PopTrade() {
  TradeInfo tradeInfo;
  if (!incomingBuffer_.Pop(tradeInfo)) {
    return false;
  }
  if (tradeInfo.type == ExecutionType::ACK) {
    PushOrderEvent({tradeInfo.clientSeq, tradeInfo.orderId, false});
    return true;
  }
  if (tradeInfo.type == ExecutionType::FULL ||
      tradeInfo.type == ExecutionType::CANCEL) {
    PushOrderEvent({tradeInfo.clientSeq, tradeInfo.orderId, true});
  }
  if (tradeInfo.type == ExecutionType::CANCEL) {
    PopCancelOrderTrade(tradeInfo);
  } else if (tradeInfo.orderType == OrderType::MARKET) {
    PopMarketOrderTrade(tradeInfo);
  } else if (tradeInfo.orderType == OrderType::LIMIT) {
    PopLimitOrderTrade(tradeInfo);
  }
  return true;
}

void Agent::PopLimitOrderTrade(TradeInfo &tradeInfo) {
  if (tradeInfo.side == Side::Sell) {
    int64_t priceCents = static_cast<std::int64_t>(tradeInfo.price * 100);
    int64_t totalCents =
        static_cast<std::int64_t>(priceCents * tradeInfo.quantity);
    availableCash_.fetch_add(totalCents, std::memory_order_relaxed);
  } else {
    int64_t priceCents = static_cast<std::int64_t>(tradeInfo.price * 100);
    int64_t totalCents =
        static_cast<std::int64_t>(priceCents * tradeInfo.quantity);
    int64_t reservedPrice =
        static_cast<std::int64_t>(tradeInfo.orderPrice * 100);
    int64_t reservedCents =
        static_cast<std::int64_t>(reservedPrice * tradeInfo.quantity);
    reservedCash_.fetch_sub(reservedCents, std::memory_order_relaxed);
    availableCash_.fetch_add(reservedCents - totalCents,
                             std::memory_order_relaxed);
    units_.fetch_add(tradeInfo.quantity, std::memory_order_relaxed);
  }
}

void Agent::PopMarketOrderTrade(TradeInfo &tradeInfo) {
  if (tradeInfo.side == Side::Sell) {
    int64_t priceCents = static_cast<std::int64_t>(tradeInfo.price * 100);
    int64_t totalCents =
        static_cast<std::int64_t>(priceCents * tradeInfo.quantity);
    availableCash_.fetch_add(totalCents, std::memory_order_relaxed);
  } else {
    int64_t priceCents = static_cast<std::int64_t>(tradeInfo.price * 100);
    int64_t totalCents =
        static_cast<std::int64_t>(priceCents * tradeInfo.quantity);
    int64_t reservedPrice =
        static_cast<std::int64_t>(tradeInfo.orderPrice * 100);
    int64_t reservedCents =
        static_cast<std::int64_t>(reservedPrice * tradeInfo.quantity);
    reservedCash_.fetch_sub(reservedCents, std::memory_order_relaxed);
    availableCash_.fetch_add(reservedCents - totalCents,
                             std::memory_order_relaxed);
    units_.fetch_add(tradeInfo.quantity, std::memory_order_relaxed);
  }
}

void Agent::PopCancelOrderTrade(TradeInfo &tradeInfo) {
  if (tradeInfo.side == Side::Sell) {
    units_.fetch_add(tradeInfo.quantity, std::memory_order_relaxed);
  } else {
    int64_t reservedPrice = static_cast<std::int64_t>(tradeInfo.price * 100);
    int64_t reservedCents =
        static_cast<std::int64_t>(reservedPrice * tradeInfo.quantity);
    reservedCash_.fetch_sub(reservedCents, std::memory_order_relaxed);
    availableCash_.fetch_add(reservedCents, std::memory_order_relaxed);
  }
}

void Agent::PrintState() {
  ClearIncoming();
  std::cout << "-- Client Ref: " << clientRef_ << " --\n";
  std::cout << "Starting  Cash: $" << initialCash_ << '\n';
  std::cout << "Current   Cash: $" << availableCash_ + reservedCash_ << '\n';
  std::cout << "Profit        : $" << (availableCash_ + reservedCash_) - cash_
            << '\n'
            << '\n';
  std::cout << "Starting  Units: " << initialUnits_ << '\n';
  std::cout << "Current   Units: " << units_ << '\n' << '\n' << '\n';
}

// Only safe once the outgoing loop has stopped (shutdown/teardown path):
// draining order events touches outgoing-thread state.
void Agent::ClearIncoming() {
  while (PopTrade()) {
  }
  DrainOrderEvents();
}

AgentInfo Agent::GetInfo() {
  AgentInfo::Strategy strategy = std::visit(
      [](auto &&strategy) -> AgentInfo::Strategy {
        using T = std::decay_t<decltype(strategy)>;
        if constexpr (std::is_same_v<T, Random>)
          return AgentInfo::Strategy::RANDOM;
        else if constexpr (std::is_same_v<T, MarketMaker>)
          return AgentInfo::Strategy::MARKETMAKER;
        else if constexpr (std::is_same_v<T, MomentumTrader>)
          return AgentInfo::Strategy::MOMENTUMTRADER;
        else if constexpr (std::is_same_v<T, MeanReverter>)
          return AgentInfo::Strategy::MEANREVERTER;
        else if constexpr (std::is_same_v<T, Whale>)
          return AgentInfo::Strategy::WHALE;
        else
          throw std::logic_error("Agent does not have a valid strategy");
      },
      strategy_);

  return AgentInfo{clientRef_, strategy, availableCash_, units_};
}
