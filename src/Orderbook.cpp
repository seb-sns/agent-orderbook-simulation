#include "Orderbook.h"
#include "Order.h"
#include "OrderPool.h"
#include "PriceLevel.h"
#include "Trade.h"
#include "TradeDispatcher.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>

Orderbook::Orderbook(OrderPool *orderPool, TradeDispatcher &tradeDispatcher)
    : orderPool_(orderPool), tradeDispatcher_(tradeDispatcher) {};

void Orderbook::setBidBit(const uint64_t index) {
  bids_bitmap_[index / 64] |= (1ULL << (index % 64));
  const uint64_t bestBidIndex = bestBidIndex_.load(std::memory_order_relaxed);
  if (bestBidIndex == INVALID_PRICE_LEVEL_INDEX || index > bestBidIndex) {
    bestBidIndex_.store(index, std::memory_order_relaxed);
  }
}

void Orderbook::setAskBit(const uint64_t index) {
  asks_bitmap_[index / 64] |= (1ULL << (index % 64));
  const uint64_t bestAskIndex = bestAskIndex_.load(std::memory_order_relaxed);
  if (bestAskIndex == INVALID_PRICE_LEVEL_INDEX || index < bestAskIndex) {
    bestAskIndex_.store(index, std::memory_order_relaxed);
  }
}

void Orderbook::clearBidBit(const uint64_t index) {
  bids_bitmap_[index / 64] &= ~(1ULL << (index % 64));
  if (index == bestBidIndex_.load(std::memory_order_relaxed)) {
    for (int i = BITMAP_SIZE - 1; i >= 0; --i) {
      if (bids_bitmap_[i] != 0) {
        bestBidIndex_.store(i * 64 + 63 - __builtin_clzll(bids_bitmap_[i]),
                            std::memory_order_relaxed);
        return;
      }
    }
    bestBidIndex_.store(INVALID_PRICE_LEVEL_INDEX, std::memory_order_relaxed);
  }
}

void Orderbook::clearAskBit(const uint64_t index) {
  asks_bitmap_[index / 64] &= ~(1ULL << (index % 64));
  if (index == bestAskIndex_.load(std::memory_order_relaxed)) {
    for (uint64_t i = 0; i < BITMAP_SIZE; ++i) {
      if (asks_bitmap_[i] != 0) {
        bestAskIndex_.store(i * 64 + __builtin_ctzll(asks_bitmap_[i]),
                            std::memory_order_relaxed);
        return;
      }
    }
    bestAskIndex_.store(INVALID_PRICE_LEVEL_INDEX, std::memory_order_relaxed);
  }
}

std::optional<uint64_t> Orderbook::GetBestBid() {
  const uint64_t bestBidIndex = bestBidIndex_.load(std::memory_order_relaxed);
  if (bestBidIndex == INVALID_PRICE_LEVEL_INDEX) {
    return std::nullopt;
  }
  return bestBidIndex;
}

std::optional<uint64_t> Orderbook::GetBestAsk() {
  const uint64_t bestAskIndex = bestAskIndex_.load(std::memory_order_relaxed);
  if (bestAskIndex == INVALID_PRICE_LEVEL_INDEX) {
    return std::nullopt;
  }
  return bestAskIndex;
}

uint64_t Orderbook::PriceToIndex(Price price) const {
  // assert(price >= 100 && price <= 120);
  if (price < 100) {
    price = 100;
  }
  if (price > 120) {
    price = 120;
  }
  double index = (price - min_price_) / tick_size_;
  return static_cast<uint64_t>(std::floor(index + 1e-9));
}

Price Orderbook::IndexToPrice(uint64_t index) const {
  // assert(index >= 0 && index <= 2000);
  if (index < 0) {
    index = 0;
  }
  if (index > 2000) {
    index = 2000;
  }
  return min_price_ + index * tick_size_;
}

void Orderbook::AddOrder(Order *order) {
  const uint64_t &index = PriceToIndex(order->GetPrice());
  const Side side = order->GetSide();
  auto &priceLevel = (side == Side::Buy) ? bids_[index] : asks_[index];

  const PoolIndex poolIndex = order->GetIndex();
  Order *oldTail = nullptr;

  if (!priceLevel.empty()) {
    oldTail = orderPool_->get_order(priceLevel.tail_);
    order->SetPrev(priceLevel.tail_);
    order->SetNext(-1);
    oldTail->SetNext(poolIndex);
    priceLevel.tail_ = poolIndex;
  } else {
    priceLevel.head_ = poolIndex;
    priceLevel.tail_ = poolIndex;
    order->SetPrev(-1);
    order->SetNext(-1);
    if (side == Side::Buy) {
      setBidBit(index);
    } else {
      setAskBit(index);
    }
  }
  orderMap_.insert({order->GetOrderId(), poolIndex});
}

void Orderbook::RemoveOrder(Order *order) {
  RemoveOrderUnlocked(order);
}

void Orderbook::RemoveOrderUnlocked(Order *order) {
  const uint64_t &index = PriceToIndex(order->GetPrice());
  auto &priceLevel =
      (order->GetSide() == Side::Buy) ? bids_[index] : asks_[index];

  PoolIndex poolIndex = order->GetIndex();
  PoolIndex prev = order->GetPrev();
  PoolIndex next = order->GetNext();

  if (prev != -1) {
    orderPool_->get_order(prev)->SetNext(next);
  }

  if (next != -1) {
    orderPool_->get_order(next)->SetPrev(prev);
  }

  if (priceLevel.head_ == poolIndex) {
    priceLevel.head_ = next;
  }

  if (priceLevel.tail_ == poolIndex) {
    priceLevel.tail_ = prev;
  }

  if (priceLevel.empty()) {
    if (order->GetSide() == Side::Buy) {
      clearBidBit(index);
    } else {
      clearAskBit(index);
    }
  }

  orderMap_.erase(order->GetOrderId());
  orderPool_->deallocate(poolIndex);
}

void Orderbook::CancelOrder(Order *cancelOrder) {
  const std::uint64_t &index{PriceToIndex(cancelOrder->GetPrice())};
  if (cancelOrder->GetSide() == Side::Buy) {
    auto &priceLevel = bids_[index];
    const PoolIndex *ptr = orderMap_.find(cancelOrder->GetOrderId());
    if (!ptr) {
      return;
    }
    // std::cout << "[Orderbook CancelOrder] - OrderId: " <<
    // cancelOrder->GetOrderId() << std::endl;
    PoolIndex poolIndex = *ptr;
    Order *order = orderPool_->get_order(poolIndex);
    TradeInfo cancelledTrade(order->GetOrderId(), order->GetOrderType(),
                       order->GetClientRef(), Side::Buy, order->GetPrice(),
                       order->GetRemainingQuantity(), *order,
                       ExecutionType::CANCEL);
    tradeDispatcher_.PushTradeInfo(std::move(cancelledTrade));
    RemoveOrderUnlocked(order);
  } else {
    auto &priceLevel = asks_[index];
    const PoolIndex *ptr = orderMap_.find(cancelOrder->GetOrderId());
    if (!ptr) {
      return;
    }
    PoolIndex poolIndex = *ptr;

    Order *order = orderPool_->get_order(poolIndex);
    TradeInfo cancelledTrade(order->GetOrderId(), order->GetOrderType(),
                       order->GetClientRef(), Side::Sell, order->GetPrice(),
                       order->GetRemainingQuantity(), *order,
                       ExecutionType::CANCEL);
    tradeDispatcher_.PushTradeInfo(std::move(cancelledTrade));
    RemoveOrderUnlocked(order);
  }
}

void Orderbook::FillOrder(Order *order, std::uint64_t index) {
  auto &matchedPriceLevel =
      (order->GetSide() == Side::Buy) ? asks_[index] : bids_[index];
  const PoolIndex matchedIndex = matchedPriceLevel.head_;
  Order *matchedOrder = orderPool_->get_order(matchedIndex);
  assert(matchedOrder != order);
  Quantity filledQuantity = matchedOrder->Fill(*order);

  ExecutionType orderExecutionType;
  ExecutionType matchedOrderExecutionType;

  if (order->isFilled()) {
    orderExecutionType = ExecutionType::FULL;
  } else {
    orderExecutionType = ExecutionType::PARTIAL;
  }

  if (matchedOrder->isFilled()) {
    matchedOrderExecutionType = ExecutionType::FULL;
  } else {
    matchedOrderExecutionType = ExecutionType::PARTIAL;
  }

  if (order->GetSide() == Side::Buy) {
    TradeInfo bidTrade(order->GetOrderId(), order->GetOrderType(),
                       order->GetClientRef(), Side::Buy,
                       matchedOrder->GetPrice(), filledQuantity, *order,
                       orderExecutionType);
    TradeInfo askTrade(matchedOrder->GetOrderId(), matchedOrder->GetOrderType(),
                       matchedOrder->GetClientRef(), Side::Sell,
                       matchedOrder->GetPrice(), filledQuantity, *matchedOrder,
                       matchedOrderExecutionType);
    Trade trade(askTrade, bidTrade);
    tradeDispatcher_.PushTradeInfo(std::move(trade));
  } else {
    TradeInfo bidTrade(matchedOrder->GetOrderId(), matchedOrder->GetOrderType(),
                       matchedOrder->GetClientRef(), Side::Buy,
                       matchedOrder->GetPrice(), filledQuantity, *matchedOrder,
                       matchedOrderExecutionType);
    TradeInfo askTrade(order->GetOrderId(), order->GetOrderType(),
                       order->GetClientRef(), Side::Sell,
                       matchedOrder->GetPrice(), filledQuantity, *order,
                       orderExecutionType);
    Trade trade(askTrade, bidTrade);
    tradeDispatcher_.PushTradeInfo(std::move(trade));
  }

  if (matchedOrder->isFilled()) {
    RemoveOrderUnlocked(matchedOrder);
  }
}

void Orderbook::PrintBook() {
  std::cout << "\n====== ASKS ======\n";
  for (size_t level = MAX_PRICE_LEVELS - 1; level > 0; --level) {
    size_t word = level / 64;
    size_t bit = level % 64;

    uint64_t present = asks_bitmap_[word];
    bool is_set = present & (1ULL << bit);
    if (!is_set) {
      continue;
    }
    Quantity quantity{0};
    auto orderIndex = asks_[level].tail_;
    while (orderIndex != -1) {
      Order *order = orderPool_->get_order(orderIndex);
      quantity += order->GetRemainingQuantity();
      orderIndex = order->GetPrev();
    }
    std::ostringstream price;
    price << "£" << std::fixed << std::setprecision(2) << IndexToPrice(level);

    std::cout << std::right << std::setw(5) << quantity << " @ "
              << std::setw(10) << price.str() << ": ";
    for (size_t j = 0; j < quantity / 250; ++j) {
      std::cout << "█";
    }
    std::cout << '\n';
  }

  std::cout << "\n====== BIDS ======\n";

  for (size_t level = MAX_PRICE_LEVELS - 1; level > 0; --level) {
    size_t word = level / 64;
    size_t bit = level % 64;

    uint64_t present = bids_bitmap_[word];
    bool is_set = present & (1ULL << bit);
    if (!is_set) {
      continue;
    }

    Quantity quantity{0};
    auto orderIndex = bids_[level].tail_;
    while (orderIndex != -1) {
      Order *order = orderPool_->get_order(orderIndex);
      quantity += order->GetRemainingQuantity();
      orderIndex = order->GetPrev();
    }
    std::ostringstream price;
    price << "£" << std::fixed << std::setprecision(2) << IndexToPrice(level);

    std::cout << std::right << std::setw(5) << quantity << " @ "
              << std::setw(10) << price.str() << ": ";
    for (size_t j = 0; j < quantity / 250; ++j) {
      std::cout << "█";
    }
    std::cout << '\n';
  }
  std::cout << '\n';
}
