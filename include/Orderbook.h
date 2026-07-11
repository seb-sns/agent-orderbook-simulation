#pragma once
#include "FlatHashMap.h"
#include "Order.h"
#include "OrderPool.h"
#include "PriceLevel.h"
#include "TradeDispatcher.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>

static constexpr int MAX_PRICE_LEVELS = 2001; // +/- 1000 levels
static constexpr uint64_t BITMAP_SIZE = (MAX_PRICE_LEVELS + 63) / 64;
static constexpr uint64_t INVALID_PRICE_LEVEL_INDEX =
    std::numeric_limits<uint64_t>::max();

class Orderbook {
public:
  // Threading model: the matching engine is the only writer. Other threads may
  // read top-of-book via GetBestBid/GetBestAsk, which are backed by atomics.
  // Full book traversal, e.g. PrintBook, should only happen when matching is
  // stopped.
  Orderbook(OrderPool *orderPool, TradeDispatcher &tradeDispatcher);
  void AddOrder(Order *order);
  void RemoveOrder(Order *order);
  void FillOrder(Order *order, uint64_t index);
  void CancelOrder(Order *cancelOrder);

  std::optional<uint64_t> GetBestBid();
  std::optional<uint64_t> GetBestAsk();
  uint64_t PriceToIndex(const Price price) const;
  Price IndexToPrice(const uint64_t index) const;

  void PrintBook();

private:
  double min_price_{100};
  double tick_size_{0.01};
  std::array<PriceLevel, MAX_PRICE_LEVELS> bids_{};
  std::array<PriceLevel, MAX_PRICE_LEVELS> asks_{};
  uint64_t bids_bitmap_[BITMAP_SIZE] = {};
  uint64_t asks_bitmap_[BITMAP_SIZE] = {};
  std::atomic<uint64_t> bestBidIndex_{INVALID_PRICE_LEVEL_INDEX};
  std::atomic<uint64_t> bestAskIndex_{INVALID_PRICE_LEVEL_INDEX};
  FlatHashMap<OrderId, PoolIndex> orderMap_{524'288};

  void setBidBit(const uint64_t index);
  void setAskBit(const uint64_t index);
  void clearBidBit(const uint64_t index);
  void clearAskBit(const uint64_t index);
  void RemoveOrderUnlocked(Order *order);

  TradeDispatcher &tradeDispatcher_;
  OrderPool *orderPool_;
};
