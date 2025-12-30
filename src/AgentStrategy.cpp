#include "AgentStrategy.h"
#include "Agent.h"
#include "Order.h"
#include "OrderPool.h"
#include <cassert>
#include <cmath>
#include <optional>
#include <random>
#include <vector>

MarketMaker::MarketMaker(Orderbook *orderbook, OrderPool *orderPool,
                         double spread)
    : orderbook_(orderbook), orderPool_(orderPool), spread_(spread) {};

OrderPtrs MarketMaker::Act(Agent *agent) {
  OrderPtrs orders{};
  OrderPtrs cancelOrders{CancelOrders(agent)};
  OrderPtrs activeOrders{CreateOrders(agent)};
  orders.insert(orders.end(), activeOrders.begin(), activeOrders.end());
  orders.insert(orders.end(), cancelOrders.begin(), cancelOrders.end());
  return orders;
}

OrderPtrs MarketMaker::CreateOrders(Agent *agent) {
  if (!agent) {
    return OrderPtrs{};
  }
  auto bestBidIndex = orderbook_->GetBestBid();
  auto bestAskIndex = orderbook_->GetBestAsk();
  if (!bestBidIndex || !bestAskIndex) {
    return OrderPtrs{};
  }
  lastMidPrice_ = midPrice_;
  midPrice_ = (orderbook_->IndexToPrice(*bestAskIndex) +
               orderbook_->IndexToPrice(*bestBidIndex)) /
              2.0;

  Price askPrice = midPrice_ + (spread_ / 2.0);
  askPrice = std::round(askPrice * 100.0) / 100.0;
  Price bidPrice = midPrice_ - (spread_ / 2.0);
  bidPrice = std::round(bidPrice * 100.0) / 100.0;

  if ((agent->GetUnits() > 10) &&
      (agent->GetAvailableCash() / 100.0 > bidPrice * 10)) {

    PoolIndex sellSideSlot = orderPool_->allocate();
    Order *sellOrder = orderPool_->get_order(sellSideSlot);
    sellOrder->SetOrderId(0);
    sellOrder->SetOrderType(OrderType::LIMIT);
    sellOrder->GetClientRef(agent->GetClientRef());
    sellOrder->SetSide(Side::Sell);
    sellOrder->SetPrice(askPrice);
    sellOrder->SetInitialQuantity(10);
    sellOrder->SetRemainingQuantity(10);
    sellOrder->SetIndex(sellSideSlot);

    PoolIndex buySideSlot = orderPool_->allocate();
    Order *buyOrder = orderPool_->get_order(buySideSlot);
    buyOrder->SetOrderId(0);
    buyOrder->SetOrderType(OrderType::LIMIT);
    buyOrder->GetClientRef(agent->GetClientRef());
    buyOrder->SetSide(Side::Buy);
    buyOrder->SetPrice(bidPrice);
    buyOrder->SetInitialQuantity(10);
    buyOrder->SetRemainingQuantity(10);
    buyOrder->SetIndex(buySideSlot);

    return OrderPtrs{buyOrder, sellOrder};
  }
  return OrderPtrs{};
}

OrderPtrs MarketMaker::CancelOrders(Agent *agent) {
  if (!agent) {
    return OrderPtrs{};
  }
  if (std::abs(midPrice_ - lastMidPrice_) <= spread_) {
    return OrderPtrs{};
  }
  OrderPtrs orders{};
  std::vector<PoolIndex> ordersToCancel{};
  const auto activeOrders = agent->GetActiveOrders();
  for (const auto &[_, order] : activeOrders) {
    if (std::abs(order->GetPrice() - midPrice_) <= spread_ * 2) {
      continue;
    }
    PoolIndex slot = orderPool_->allocate();
    Order *cancelOrder = orderPool_->get_order(slot);
    cancelOrder->SetOrderId(order->GetOrderId());
    cancelOrder->SetOrderType(OrderType::CANCEL);
    cancelOrder->GetClientRef(agent->GetClientRef());
    cancelOrder->SetSide(order->GetSide());
    cancelOrder->SetPrice(order->GetPrice());
    cancelOrder->SetInitialQuantity(0);
    cancelOrder->SetRemainingQuantity(0);
    cancelOrder->SetIndex(slot);

    ordersToCancel.push_back(order->GetIndex());
    orders.push_back(cancelOrder);
  }

  for (const auto &slotIndex : ordersToCancel) {
    agent->RemoveActiveOrder(slotIndex);
  }

  return orders;
}

MomentumTrader::MomentumTrader(Orderbook *orderbook, OrderPool *orderPool,
                               double threshold)
    : orderbook_(orderbook), orderPool_(orderPool), threshold_(threshold) {};

OrderPtrs MomentumTrader::Act(Agent *agent) {
  OrderPtrs orders{};
  OrderPtrs activeOrders{CreateOrders(agent)};
  orders.insert(orders.end(), activeOrders.begin(), activeOrders.end());
  return orders;
}

OrderPtrs MomentumTrader::CreateOrders(Agent *agent) {
  if (!agent) {
    return OrderPtrs{};
  }
  auto bestBidIndex = orderbook_->GetBestBid();
  auto bestAskIndex = orderbook_->GetBestAsk();
  if (!bestBidIndex || !bestAskIndex) {
    return OrderPtrs{};
  }
  const double midPrice = (orderbook_->IndexToPrice(*bestAskIndex) +
                     orderbook_->IndexToPrice(*bestBidIndex)) /
                    2;

  shortTermObservations_.Push(midPrice);
  longTermObservations_.Push(midPrice);

  if (!shortTermObservations_.full()) {
    return OrderPtrs{};
  }

  if (!longTermObservations_.full()) {
    return OrderPtrs{};
  }
  double shortTermLeavingObs{0};
  shortTermObservations_.Pop(shortTermLeavingObs);
  shortTermSum_ += (midPrice - shortTermLeavingObs);
  double shortTermMovingAverage_ =
      shortTermSum_ / shortTermObservations_.size();

  double longTermLeavingObs{0};
  longTermObservations_.Pop(longTermLeavingObs);
  longTermSum_ += (midPrice - longTermLeavingObs);
  double longTermMovingAverage_ = longTermSum_ / longTermObservations_.size();

  OrderPtrs orders{};
  // Check the we have the maximum amount that could be required
  if (((agent->GetAvailableCash() / 100.0) > (10 * 120)) &&
      threshold_ <
          shortTermMovingAverage_ -
              longTermMovingAverage_) {

    PoolIndex slot = orderPool_->allocate();
    Order *order = orderPool_->get_order(slot);
    order->SetOrderId(0);
    order->SetOrderType(OrderType::MARKET);
    order->GetClientRef(agent->GetClientRef());
    order->SetSide(Side::Buy);
    order->SetPrice(120);
    order->SetInitialQuantity(10);
    order->SetRemainingQuantity(10);
    order->SetIndex(slot);

    return OrderPtrs{order};

  } else if ((agent->GetUnits() > 10) &&
             -threshold_ > shortTermMovingAverage_ - longTermMovingAverage_) {
    PoolIndex slot = orderPool_->allocate();
    Order *order = orderPool_->get_order(slot);
    order->SetOrderId(0);
    order->SetOrderType(OrderType::MARKET);
    order->GetClientRef(agent->GetClientRef());
    order->SetSide(Side::Sell);
    order->SetPrice(100);
    order->SetInitialQuantity(10);
    order->SetRemainingQuantity(10);
    order->SetIndex(slot);

    return OrderPtrs{order};
  }
  return OrderPtrs{};
}

OrderPtrs MomentumTrader::CancelOrders(Agent *agent) {
  // TO-DO
  // Currently MomentumTrader only places Market orders which will be cancelled if they can't find a match
  // Thus, there are no 'active orders' waiting to be filled/cancelled from the MomentumTrader
  return OrderPtrs{};
}

Random::Random(Orderbook *orderbook, OrderPool *orderPool, double sigma)
    : orderbook_(orderbook), orderPool_(orderPool), sigma_(sigma),
      normal_distribution_(0, sigma), bernoulli_distribution_(0.5) {};

static std::random_device rd;
static std::mt19937 gen(rd());
static std::bernoulli_distribution cancelDist =
    std::bernoulli_distribution(0.05);

OrderPtrs Random::Act(Agent *agent) {
  OrderPtrs orders{};
  OrderPtrs cancelOrders{CancelOrders(agent)};
  OrderPtrs activeOrders{CreateOrders(agent)};
  orders.insert(orders.end(), activeOrders.begin(), activeOrders.end());
  orders.insert(orders.end(), cancelOrders.begin(), cancelOrders.end());
  return orders;
}

OrderPtrs Random::CreateOrders(Agent *agent) {
  if (!agent) {
    return OrderPtrs{};
  }
  auto bestBidIndex = orderbook_->GetBestBid();
  auto bestAskIndex = orderbook_->GetBestAsk();
  double midPrice;
  if (!bestBidIndex || !bestAskIndex) {
    midPrice = 110;
  } else {
    midPrice = (orderbook_->IndexToPrice(*bestAskIndex) +
                orderbook_->IndexToPrice(*bestBidIndex)) /
               2.0;
  }

  bool side_result = bernoulli_distribution_(gen);
  Price price = midPrice + normal_distribution_(gen);
  price = std::round(price * 100.0) / 100.0;
  Quantity quantity = 10;

  Side side;
  if (side_result) {
    side = Side::Buy;
    if (agent->GetAvailableCash() / 100.0 < price) {
      return OrderPtrs{};
    }
  } else {
    side = Side::Sell;
    if (agent->GetUnits() < 1) {
      return OrderPtrs{};
    }
  }

  PoolIndex slot = orderPool_->allocate();
  Order *order = orderPool_->get_order(slot);
  order->SetOrderId(0);
  order->SetOrderType(OrderType::LIMIT);
  order->GetClientRef(agent->GetClientRef());
  order->SetSide(side);
  order->SetPrice(price);
  order->SetInitialQuantity(10);
  order->SetRemainingQuantity(10);
  order->SetIndex(slot);
  return OrderPtrs{order};
}

OrderPtrs Random::CancelOrders(Agent *agent) {
  if (!agent) {
    return OrderPtrs{};
  }
  OrderPtrs orders{};
  std::vector<PoolIndex> ordersToCancel{};
  const auto activeOrders = agent->GetActiveOrders();
  for (const auto &[_, order] : activeOrders) {
    if (cancelDist(gen)) {
      PoolIndex slot = orderPool_->allocate();
      Order *cancelOrder = orderPool_->get_order(slot);
      cancelOrder->SetOrderId(order->GetOrderId());
      cancelOrder->SetOrderType(OrderType::CANCEL);
      cancelOrder->GetClientRef(agent->GetClientRef());
      cancelOrder->SetSide(order->GetSide());
      cancelOrder->SetPrice(order->GetPrice());
      cancelOrder->SetInitialQuantity(0);
      cancelOrder->SetRemainingQuantity(0);
      cancelOrder->SetIndex(slot);

      ordersToCancel.push_back(order->GetIndex());
      orders.push_back(cancelOrder);
    }
  }

  for (const auto &slotIndex : ordersToCancel) {
    agent->RemoveActiveOrder(slotIndex);
  }

  return orders;
}
