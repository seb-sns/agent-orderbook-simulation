#include "AgentStrategy.h"
#include "Agent.h"
#include "Order.h"
#include "OrderPool.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <optional>
#include <random>
#include <vector>

MarketMaker::MarketMaker(Orderbook *orderbook, OrderPool *orderPool,
                         double spread)
    : orderbook_(orderbook), orderPool_(orderPool), spread_(spread) {};

// Cancel-and-replace: every action withdraws all resting quotes, then places
// a fresh pair around the current mid, so stale quotes never accumulate.
OrderPtrs MarketMaker::Act(Agent *agent) {
  OrderPtrs orders{CancelOrders(agent)};
  OrderPtrs activeOrders{CreateOrders(agent)};
  orders.insert(orders.end(), activeOrders.begin(), activeOrders.end());
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
  const double midPrice = (orderbook_->IndexToPrice(*bestAskIndex) +
                           orderbook_->IndexToPrice(*bestBidIndex)) /
                          2.0;

  Price askPrice = midPrice + (spread_ / 2.0);
  askPrice = std::round(askPrice * 100.0) / 100.0;
  Price bidPrice = midPrice - (spread_ / 2.0);
  bidPrice = std::round(bidPrice * 100.0) / 100.0;

  // Inventory skew: quote more size on the side that sheds inventory, so the
  // maker leans back towards a flat book.
  constexpr Quantity baseQuantity = 10;
  const double skew = std::clamp(
      (agent->GetUnits() - agent->GetInitialUnits()) / 1000.0, -1.0, 1.0);
  const Quantity askQuantity = static_cast<Quantity>(
      std::clamp(std::round(baseQuantity * (1.0 + skew)), 1.0, 20.0));
  const Quantity bidQuantity = static_cast<Quantity>(
      std::clamp(std::round(baseQuantity * (1.0 - skew)), 1.0, 20.0));

  if ((agent->GetUnits() >= askQuantity) &&
      (agent->GetAvailableCash() / 100.0 > bidPrice * bidQuantity)) {

    PoolIndex sellSideSlot = orderPool_->allocate();
    Order *sellOrder = orderPool_->get_order(sellSideSlot);
    sellOrder->SetOrderId(0);
    sellOrder->SetOrderType(OrderType::LIMIT);
    sellOrder->SetClientRef(agent->GetClientRef());
    sellOrder->SetSide(Side::Sell);
    sellOrder->SetPrice(askPrice);
    sellOrder->SetInitialQuantity(askQuantity);
    sellOrder->SetRemainingQuantity(askQuantity);
    sellOrder->SetIndex(sellSideSlot);

    PoolIndex buySideSlot = orderPool_->allocate();
    Order *buyOrder = orderPool_->get_order(buySideSlot);
    buyOrder->SetOrderId(0);
    buyOrder->SetOrderType(OrderType::LIMIT);
    buyOrder->SetClientRef(agent->GetClientRef());
    buyOrder->SetSide(Side::Buy);
    buyOrder->SetPrice(bidPrice);
    buyOrder->SetInitialQuantity(bidQuantity);
    buyOrder->SetRemainingQuantity(bidQuantity);
    buyOrder->SetIndex(buySideSlot);

    return OrderPtrs{buyOrder, sellOrder};
  }
  return OrderPtrs{};
}

OrderPtrs MarketMaker::CancelOrders(Agent *agent) {
  if (!agent) {
    return OrderPtrs{};
  }
  OrderPtrs orders{};
  agent->ForEachActiveOrder([&](const ActiveOrder &active) {
    if (active.id == 0) {
      return; // not yet acknowledged by the engine — nothing to cancel
    }
    PoolIndex slot = orderPool_->allocate();
    Order *cancelOrder = orderPool_->get_order(slot);
    cancelOrder->SetOrderId(active.id);
    cancelOrder->SetOrderType(OrderType::CANCEL);
    cancelOrder->SetClientRef(agent->GetClientRef());
    cancelOrder->SetSide(active.side);
    cancelOrder->SetPrice(active.price);
    cancelOrder->SetInitialQuantity(0);
    cancelOrder->SetRemainingQuantity(0);
    cancelOrder->SetIndex(slot);

    orders.push_back(cancelOrder);
  });

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

  if (shortTermObservations_.full()) {
    double shortTermLeavingObs{0};
    shortTermObservations_.Pop(shortTermLeavingObs);
    shortTermSum_ -= shortTermLeavingObs;
  }
  shortTermObservations_.Push(midPrice);
  shortTermSum_ += midPrice;

  if (longTermObservations_.full()) {
    double longTermLeavingObs{0};
    longTermObservations_.Pop(longTermLeavingObs);
    longTermSum_ -= longTermLeavingObs;
  }
  longTermObservations_.Push(midPrice);
  longTermSum_ += midPrice;

  if (!shortTermObservations_.full()) {
    return OrderPtrs{};
  }

  if (!longTermObservations_.full()) {
    return OrderPtrs{};
  }

  double shortTermMovingAverage_ =
      shortTermSum_ / shortTermObservations_.size();
  double longTermMovingAverage_ = longTermSum_ / longTermObservations_.size();

  // Relative divergence: threshold_ is a fraction of price (0.005 = 0.5%),
  // and conviction (order size) scales with how far past it the signal is.
  const double divergence =
      (shortTermMovingAverage_ - longTermMovingAverage_) /
      longTermMovingAverage_;
  const double strength = std::min(std::abs(divergence) / threshold_, 5.0);
  const Quantity quantity =
      static_cast<Quantity>(std::clamp(std::round(10.0 * strength), 10.0, 50.0));

  // Check the we have the maximum amount that could be required
  if (((agent->GetAvailableCash() / 100.0) > (quantity * 120)) &&
      divergence > threshold_) {

    PoolIndex slot = orderPool_->allocate();
    Order *order = orderPool_->get_order(slot);
    order->SetOrderId(0);
    order->SetOrderType(OrderType::MARKET);
    order->SetClientRef(agent->GetClientRef());
    order->SetSide(Side::Buy);
    order->SetPrice(120);
    order->SetInitialQuantity(quantity);
    order->SetRemainingQuantity(quantity);
    order->SetIndex(slot);

    return OrderPtrs{order};

  } else if ((agent->GetUnits() >= quantity) && divergence < -threshold_) {
    PoolIndex slot = orderPool_->allocate();
    Order *order = orderPool_->get_order(slot);
    order->SetOrderId(0);
    order->SetOrderType(OrderType::MARKET);
    order->SetClientRef(agent->GetClientRef());
    order->SetSide(Side::Sell);
    order->SetPrice(100);
    order->SetInitialQuantity(quantity);
    order->SetRemainingQuantity(quantity);
    order->SetIndex(slot);

    return OrderPtrs{order};
  }
  return OrderPtrs{};
}

OrderPtrs MomentumTrader::CancelOrders(Agent *agent) {
  // Places Market orders which will be cancelled if they can't find a match
  // Thus, there are no 'active orders' waiting to be filled/cancelled from the
  // MomentumTrader
  return OrderPtrs{};
}

Random::Random(Orderbook *orderbook, OrderPool *orderPool, double sigma)
    : orderbook_(orderbook), orderPool_(orderPool), sigma_(sigma),
      normal_distribution_(0, sigma), bernoulli_distribution_(0.5),
      quantityDistribution_(1, 20) {};

static std::mt19937 gen(12345);
static std::bernoulli_distribution cancelDist =
    std::bernoulli_distribution(0.05);

MeanReverter::MeanReverter(Orderbook *orderbook, OrderPool *orderPool,
                           double fairValue, double band, double adaptRate)
    : orderbook_(orderbook), orderPool_(orderPool), fairValue_(fairValue),
      band_(band), adaptRate_(adaptRate) {};

OrderPtrs MeanReverter::Act(Agent *agent) {
  auto bestBidIndex = orderbook_->GetBestBid();
  auto bestAskIndex = orderbook_->GetBestAsk();
  if (bestBidIndex && bestAskIndex) {
    const double mid = (orderbook_->IndexToPrice(*bestBidIndex) +
                        orderbook_->IndexToPrice(*bestAskIndex)) /
                       2.0;
    fairValue_ += adaptRate_ * (mid - fairValue_);
  }
  OrderPtrs orders{CancelOrders(agent)};
  OrderPtrs activeOrders{CreateOrders(agent)};
  orders.insert(orders.end(), activeOrders.begin(), activeOrders.end());
  return orders;
}

OrderPtrs MeanReverter::CreateOrders(Agent *agent) {
  if (!agent) {
    return OrderPtrs{};
  }
  auto bestBidIndex = orderbook_->GetBestBid();
  auto bestAskIndex = orderbook_->GetBestAsk();
  if (!bestBidIndex || !bestAskIndex) {
    return OrderPtrs{};
  }
  const Price bestBidPrice = orderbook_->IndexToPrice(*bestBidIndex);
  const Price bestAskPrice = orderbook_->IndexToPrice(*bestAskIndex);
  const double deviation = (bestBidPrice + bestAskPrice) / 2.0 - fairValue_;
  // Conviction scales with how deep the mispricing is relative to the band.
  const double strength = std::min(std::abs(deviation) / band_, 5.0);
  const Quantity quantity =
      static_cast<Quantity>(std::clamp(std::round(10.0 * strength), 10.0, 50.0));

  Side side;
  Price price;
  if (deviation < -band_) {
    // Undervalued: lift the ask.
    side = Side::Buy;
    price = bestAskPrice;
    if (agent->GetAvailableCash() / 100.0 < price * quantity) {
      return OrderPtrs{};
    }
  } else if (deviation > band_) {
    // Overvalued: hit the bid.
    side = Side::Sell;
    price = bestBidPrice;
    if (agent->GetUnits() < quantity) {
      return OrderPtrs{};
    }
  } else {
    return OrderPtrs{};
  }

  PoolIndex slot = orderPool_->allocate();
  Order *order = orderPool_->get_order(slot);
  order->SetOrderId(0);
  order->SetOrderType(OrderType::LIMIT);
  order->SetClientRef(agent->GetClientRef());
  order->SetSide(side);
  order->SetPrice(price);
  order->SetInitialQuantity(quantity);
  order->SetRemainingQuantity(quantity);
  order->SetIndex(slot);
  return OrderPtrs{order};
}

OrderPtrs MeanReverter::CancelOrders(Agent *agent) {
  if (!agent) {
    return OrderPtrs{};
  }
  auto bestBidIndex = orderbook_->GetBestBid();
  auto bestAskIndex = orderbook_->GetBestAsk();
  if (!bestBidIndex || !bestAskIndex) {
    return OrderPtrs{};
  }
  const double mid = (orderbook_->IndexToPrice(*bestAskIndex) +
                      orderbook_->IndexToPrice(*bestBidIndex)) /
                     2.0;
  const double deviation = mid - fairValue_;

  OrderPtrs orders{};
  agent->ForEachActiveOrder([&](const ActiveOrder &active) {
    if (active.id == 0) {
      return; // not yet acknowledged by the engine — nothing to cancel
    }
    // Keep an order only while it still expresses the current stance.
    if ((deviation < -band_ && active.side == Side::Buy) ||
        (deviation > band_ && active.side == Side::Sell)) {
      return;
    }
    PoolIndex slot = orderPool_->allocate();
    Order *cancelOrder = orderPool_->get_order(slot);
    cancelOrder->SetOrderId(active.id);
    cancelOrder->SetOrderType(OrderType::CANCEL);
    cancelOrder->SetClientRef(agent->GetClientRef());
    cancelOrder->SetSide(active.side);
    cancelOrder->SetPrice(active.price);
    cancelOrder->SetInitialQuantity(0);
    cancelOrder->SetRemainingQuantity(0);
    cancelOrder->SetIndex(slot);

    orders.push_back(cancelOrder);
  });
  return orders;
}

Whale::Whale(Orderbook *orderbook, OrderPool *orderPool, Quantity orderSize)
    : orderbook_(orderbook), orderPool_(orderPool), sideDistribution_(0.5),
      quantityDistribution_(std::max<Quantity>(1, orderSize / 2),
                            orderSize * 2) {};

OrderPtrs Whale::Act(Agent *agent) { return CreateOrders(agent); }

OrderPtrs Whale::CreateOrders(Agent *agent) {
  if (!agent) {
    return OrderPtrs{};
  }

  const Quantity quantity = quantityDistribution_(gen);
  Side side;
  Price price;
  if (sideDistribution_(gen)) {
    side = Side::Buy;
    price = 120; // clamp prices mirror the orderbook's grid bounds
    if (agent->GetAvailableCash() / 100.0 < price * quantity) {
      return OrderPtrs{};
    }
  } else {
    side = Side::Sell;
    price = 100;
    if (agent->GetUnits() < quantity) {
      return OrderPtrs{};
    }
  }

  PoolIndex slot = orderPool_->allocate();
  Order *order = orderPool_->get_order(slot);
  order->SetOrderId(0);
  order->SetOrderType(OrderType::MARKET);
  order->SetClientRef(agent->GetClientRef());
  order->SetSide(side);
  order->SetPrice(price);
  order->SetInitialQuantity(quantity);
  order->SetRemainingQuantity(quantity);
  order->SetIndex(slot);
  return OrderPtrs{order};
}

OrderPtrs Whale::CancelOrders(Agent *agent) {
  // Market orders are cancelled by the engine if they can't match, so there
  // are no resting orders to manage.
  return OrderPtrs{};
}

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
  Quantity quantity = quantityDistribution_(gen);

  Side side;
  if (side_result) {
    side = Side::Buy;
    if (agent->GetAvailableCash() / 100.0 < price * quantity) {
      return OrderPtrs{};
    }
  } else {
    side = Side::Sell;
    if (agent->GetUnits() < quantity) {
      return OrderPtrs{};
    }
  }

  PoolIndex slot = orderPool_->allocate();
  Order *order = orderPool_->get_order(slot);
  order->SetOrderId(0);
  order->SetOrderType(OrderType::LIMIT);
  order->SetClientRef(agent->GetClientRef());
  order->SetSide(side);
  order->SetPrice(price);
  order->SetInitialQuantity(quantity);
  order->SetRemainingQuantity(quantity);
  order->SetIndex(slot);
  return OrderPtrs{order};
}

OrderPtrs Random::CancelOrders(Agent *agent) {
  if (!agent) {
    return OrderPtrs{};
  }
  OrderPtrs orders{};
  agent->ForEachActiveOrder([&](const ActiveOrder &active) {
    if (active.id != 0 && cancelDist(gen)) {
      PoolIndex slot = orderPool_->allocate();
      Order *cancelOrder = orderPool_->get_order(slot);
      cancelOrder->SetOrderId(active.id);
      cancelOrder->SetOrderType(OrderType::CANCEL);
      cancelOrder->SetClientRef(agent->GetClientRef());
      cancelOrder->SetSide(active.side);
      cancelOrder->SetPrice(active.price);
      cancelOrder->SetInitialQuantity(0);
      cancelOrder->SetRemainingQuantity(0);
      cancelOrder->SetIndex(slot);

      orders.push_back(cancelOrder);
    }
  });
  return orders;
}
