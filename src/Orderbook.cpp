#include "Order.h"
#include "OrderPool.h"
#include "Orderbook.h"
#include "PriceLevel.h"
#include "Trade.h"
#include "TradeDispatcher.h"
#include "VizRecorder.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <unistd.h>
#include <vector>

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
  double index = static_cast<uint64_t>(price * 100.0 + 0.5) - 10000;
  return index;
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
  const uint64_t index = PriceToIndex(order->GetPrice());
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
  orderMap_.insert(order->GetOrderId(), poolIndex);
  VIZ_EMIT_ADD(order->GetOrderId(), order->GetClientRef(), index,
               order->GetRemainingQuantity(), side);

  // Echo the engine-assigned order id back to the agent so it can cancel
  // this resting order later without reading shared pool memory.
  TradeInfo ack{.price = order->GetPrice(),
                .orderPrice = order->GetPrice(),
                .orderId = order->GetOrderId(),
                .clientRef = static_cast<std::uint32_t>(order->GetClientRef()),
                .clientSeq = static_cast<std::uint32_t>(order->GetTimestamp()),
                .quantity = order->GetRemainingQuantity(),
                .orderType = order->GetOrderType(),
                .side = side,
                .type = ExecutionType::ACK};
  tradeDispatcher_.PushTradeInfo(std::move(ack));
}

void Orderbook::RemoveOrder(Order *order) { RemoveOrderUnlocked(order); }

void Orderbook::RemoveOrderUnlocked(Order *order) {
  const uint64_t index = PriceToIndex(order->GetPrice());
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
  const PoolIndex *ptr = orderMap_.find(cancelOrder->GetOrderId());
  if (!ptr) {
    return;
  }
  PoolIndex poolIndex = *ptr;
  Order *order = orderPool_->get_order(poolIndex);
  TradeInfo cancelledTrade{
      .price = order->GetPrice(),
      .orderPrice = order->GetPrice(),
      .orderId = order->GetOrderId(),
      .clientRef = static_cast<std::uint32_t>(order->GetClientRef()),
      .clientSeq = static_cast<std::uint32_t>(order->GetTimestamp()),
      .quantity = order->GetRemainingQuantity(),
      .orderType = order->GetOrderType(),
      .side = order->GetSide(),
      .type = ExecutionType::CANCEL};
  tradeDispatcher_.PushTradeInfo(std::move(cancelledTrade));
  VIZ_EMIT_CANCEL(order->GetOrderId(), order->GetClientRef(),
                  PriceToIndex(order->GetPrice()),
                  order->GetRemainingQuantity(), order->GetSide());
  RemoveOrderUnlocked(order);
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

  const auto makeLeg = [&](const Order *legOrder, Side legSide,
                           ExecutionType executionType) {
    return TradeInfo{
        .price = matchedOrder->GetPrice(),
        .orderPrice = legOrder->GetPrice(),
        .orderId = legOrder->GetOrderId(),
        .clientRef = static_cast<std::uint32_t>(legOrder->GetClientRef()),
        .clientSeq = static_cast<std::uint32_t>(legOrder->GetTimestamp()),
        .quantity = filledQuantity,
        .orderType = legOrder->GetOrderType(),
        .side = legSide,
        .type = executionType};
  };
  if (order->GetSide() == Side::Buy) {
    Trade trade(makeLeg(matchedOrder, Side::Sell, matchedOrderExecutionType),
                makeLeg(order, Side::Buy, orderExecutionType));
    tradeDispatcher_.PushTradeInfo(std::move(trade));
  } else {
    Trade trade(makeLeg(order, Side::Sell, orderExecutionType),
                makeLeg(matchedOrder, Side::Buy, matchedOrderExecutionType));
    tradeDispatcher_.PushTradeInfo(std::move(trade));
  }

  VIZ_EMIT_TRADE(matchedOrder->GetOrderId(), order->GetClientRef(),
                 matchedOrder->GetClientRef(), index, filledQuantity,
                 order->GetSide());

  if (matchedOrder->isFilled()) {
    RemoveOrderUnlocked(matchedOrder);
  }
}

// Compact end-of-run book view: the N levels closest to the touch on each
// side, with per-level order counts and bars scaled to the largest shown
// level. Farther levels are summarised. Colored when stdout is a terminal.
void Orderbook::PrintBook() {
  constexpr int SHOW = 12;
  constexpr int BAR_WIDTH = 30;

  const bool color = isatty(fileno(stdout)) != 0;
  const char *RED = color ? "\033[31m" : "";
  const char *GREEN = color ? "\033[32m" : "";
  const char *DIM = color ? "\033[90m" : "";
  const char *RESET = color ? "\033[0m" : "";

  struct Level {
    uint64_t index;
    Quantity quantity;
    int orders;
  };
  auto sumLevel = [&](const PriceLevel &priceLevel) {
    Level level{0, 0, 0};
    auto orderIndex = priceLevel.tail_;
    while (orderIndex != -1) {
      Order *order = orderPool_->get_order(orderIndex);
      level.quantity += order->GetRemainingQuantity();
      ++level.orders;
      orderIndex = order->GetPrev();
    }
    return level;
  };

  std::vector<Level> askLevels, bidLevels;
  std::uint64_t askUnitsBeyond = 0, bidUnitsBeyond = 0;
  int askLevelsBeyond = 0, bidLevelsBeyond = 0;
  for (uint64_t i = 0; i < MAX_PRICE_LEVELS; ++i) {
    if (!(asks_bitmap_[i / 64] & (1ULL << (i % 64)))) {
      continue;
    }
    Level level = sumLevel(asks_[i]);
    if (level.quantity == 0) {
      continue;
    }
    level.index = i;
    if (askLevels.size() < SHOW) {
      askLevels.push_back(level);
    } else {
      ++askLevelsBeyond;
      askUnitsBeyond += level.quantity;
    }
  }
  for (uint64_t i = MAX_PRICE_LEVELS; i-- > 0;) {
    if (!(bids_bitmap_[i / 64] & (1ULL << (i % 64)))) {
      continue;
    }
    Level level = sumLevel(bids_[i]);
    if (level.quantity == 0) {
      continue;
    }
    level.index = i;
    if (bidLevels.size() < SHOW) {
      bidLevels.push_back(level);
    } else {
      ++bidLevelsBeyond;
      bidUnitsBeyond += level.quantity;
    }
  }

  std::cout << "\n────────────────────── ORDERBOOK ──────────────────────\n";
  if (askLevels.empty() && bidLevels.empty()) {
    std::cout << "  (empty)\n\n";
    return;
  }

  Quantity maxQuantity = 1;
  for (const Level &level : askLevels) {
    maxQuantity = std::max(maxQuantity, level.quantity);
  }
  for (const Level &level : bidLevels) {
    maxQuantity = std::max(maxQuantity, level.quantity);
  }

  auto printLevel = [&](const Level &level, const char *sideColor) {
    const int bar = std::max<int>(
        1, static_cast<int>(static_cast<double>(level.quantity) /
                            maxQuantity * BAR_WIDTH));
    std::cout << "  " << std::fixed << std::setprecision(2) << std::setw(8)
              << IndexToPrice(level.index) << std::setw(7) << level.quantity
              << std::setw(5) << level.orders << "  " << sideColor;
    for (int j = 0; j < bar; ++j) {
      std::cout << "█";
    }
    std::cout << RESET << '\n';
  };

  std::cout << DIM << "     price    qty  ord\n" << RESET;
  if (askLevelsBeyond > 0) {
    std::cout << DIM << "  (+" << askLevelsBeyond << " more ask levels, "
              << askUnitsBeyond << " units above)\n" << RESET;
  }
  for (auto it = askLevels.rbegin(); it != askLevels.rend(); ++it) {
    printLevel(*it, RED);
  }
  if (!askLevels.empty() && !bidLevels.empty()) {
    const double bestAsk = IndexToPrice(askLevels.front().index);
    const double bestBid = IndexToPrice(bidLevels.front().index);
    std::cout << DIM << "  ── mid " << std::fixed << std::setprecision(3)
              << (bestAsk + bestBid) / 2.0 << " · spread "
              << std::setprecision(2) << (bestAsk - bestBid) << " ──\n"
              << RESET;
  }
  for (const Level &level : bidLevels) {
    printLevel(level, GREEN);
  }
  if (bidLevelsBeyond > 0) {
    std::cout << DIM << "  (+" << bidLevelsBeyond << " more bid levels, "
              << bidUnitsBeyond << " units below)\n" << RESET;
  }
  std::cout << '\n';
}
