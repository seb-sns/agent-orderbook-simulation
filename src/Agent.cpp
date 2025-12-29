#include "Agent.h"
#include "AgentStrategy.h"
#include "MatchingEngine.h"
#include "Order.h"
#include "OrderPool.h"
#include "Trade.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
#include <variant>

static std::random_device rd;
static std::mt19937 gen(rd());
static std::uniform_real_distribution<> dis(0.0, 1.0);

double sampleExponential(double rate) {
  double U = dis(gen);
  return -std::log(U) / rate;
}

Agent::Agent(TradeDispatcher &tradeDispatcher, MatchingEngine &matchingEngine,
             AgentStrategy strategy, ClientRef clientRef, double rate)
    : strategy_(strategy), clientRef_(clientRef), rate_(rate),
      matchingEngine_(matchingEngine), tradeDispatcher_(tradeDispatcher) {
  tradeDispatcher_.Attach(this);
};

Agent::~Agent() {
  ClearIncoming();
  tradeDispatcher_.Detach(this);
}

void Agent::AddActiveOrder(PoolIndex index, Order *order) {
  std::unique_lock<std::shared_mutex> lock(mtx_);
  (activeOrders_)[index] = order;
}

void Agent::RemoveActiveOrder(PoolIndex index) {
  std::unique_lock<std::shared_mutex> lock(mtx_);
  activeOrders_.erase(index);
}

std::unordered_map<PoolIndex, Order *>
Agent::GetActiveOrders() const {
  std::shared_lock<std::shared_mutex> lock(mtx_);
  return activeOrders_;
}

OrderPtrs Agent::Act() {
  return std::visit(
      [&](auto activeStrategy) { return activeStrategy.Act(this); }, strategy_);
}

double Agent::ScheduleNextAction(std::uint64_t currentTime) {
  return currentTime + sampleExponential(rate_);
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

void Agent::PushLimitOrder(Order *order) {
  AddActiveOrder(order->GetIndex(), order);
  if (order->GetSide() == Side::Sell) {
    units_.fetch_sub(order->GetRemainingQuantity(),
                     std::memory_order_relaxed);
  } else {
    int64_t priceCents = static_cast<std::int64_t>(order->GetPrice() * 100);
    int64_t totalCents =
        static_cast<std::int64_t>(priceCents * order->GetRemainingQuantity());
    availableCash_.fetch_sub(totalCents, std::memory_order_relaxed);
    reservedCash_.fetch_add(totalCents, std::memory_order_relaxed);
  }
  matchingEngine_.orders_.Push(std::move(order));
}

void Agent::PushMarketOrder(Order *order) {
  AddActiveOrder(order->GetIndex(), order);
  if (order->GetSide() == Side::Sell) {
    units_.fetch_sub(order->GetRemainingQuantity(),
                     std::memory_order_relaxed);
  } else {
    int64_t priceCents = static_cast<std::int64_t>(order->GetPrice() * 100);
    int64_t totalCents =
        static_cast<std::int64_t>(priceCents * order->GetRemainingQuantity());
    availableCash_.fetch_sub(totalCents, std::memory_order_relaxed);
    reservedCash_.fetch_add(totalCents, std::memory_order_relaxed);
  }
  matchingEngine_.orders_.Push(std::move(order));
}

void Agent::PushCancelOrder(Order* order) {
  matchingEngine_.orders_.Push(std::move(order));
}

void Agent::PushTrade(TradeInfo &&tradeInfo) {
  incomingBuffer_.Push(std::move(tradeInfo));
}

void Agent::PopTrade() {
  TradeInfo tradeInfo;
  if (incomingBuffer_.Pop(tradeInfo)) {
    if (tradeInfo.type == ExecutionType::FULL || tradeInfo.type == ExecutionType::CANCEL) {
      RemoveActiveOrder(tradeInfo.order.GetOrderId());
    }
    if (tradeInfo.type == ExecutionType::CANCEL) {
      PopCancelOrderTrade(tradeInfo);
    } else if (tradeInfo.orderType == OrderType::MARKET) {
      PopMarketOrderTrade(tradeInfo);
    } else if (tradeInfo.orderType == OrderType::LIMIT) {
      PopLimitOrderTrade(tradeInfo);
    }
  }
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
        static_cast<std::int64_t>(tradeInfo.order.GetPrice() * 100);
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
        static_cast<std::int64_t>(tradeInfo.order.GetPrice() * 100);
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
    int64_t reservedPrice =
        static_cast<std::int64_t>(tradeInfo.price * 100);
    int64_t reservedCents =
        static_cast<std::int64_t>(reservedPrice * tradeInfo.quantity);
    reservedCash_.fetch_sub(reservedCents, std::memory_order_relaxed);
    availableCash_.fetch_add(reservedCents, std::memory_order_relaxed);
  }
}

void Agent::PrintState() {
  while (!incomingBuffer_.empty()) {
    PopTrade();
  }
  std::cout << "-- Client Ref: " << clientRef_ << " --\n";
  std::cout << "Starting  Cash: $" << initialCash_ << '\n';
  std::cout << "Current   Cash: $" << availableCash_ + reservedCash_ << '\n';
  std::cout << "Profit        : $" << (availableCash_ + reservedCash_) - cash_
            << '\n'
            << '\n';
  std::cout << "Starting  Units: " << initialUnits_ << '\n';
  std::cout << "Current   Units: " << units_ << '\n' << '\n' << '\n';
}

void Agent::ClearIncoming() {
  while (!incomingBuffer_.empty()) {
    PopTrade();
  }
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
        else
          throw std::logic_error("Agent does not have a valid strategy");
      },
      strategy_);

  return AgentInfo{clientRef_, strategy, availableCash_, units_};
}
