#pragma once

#include "Order.h"
#include <cstdint>

// ACK: the engine accepted a limit order onto the book — carries the
// engine-assigned orderId back to the agent so it can cancel later.
enum class ExecutionType : std::uint8_t { CANCEL, PARTIAL, FULL, INVALID, ACK };

// Packed to one cache line: this struct is constructed twice per fill and
// copied through the dispatcher and each agent's incoming ring. clientRef
// and clientSeq are narrowed to 32 bits (≤4B agents / orders-per-agent).
struct TradeInfo {
  Price price;      // executed price
  Price orderPrice; // price the order was submitted (reserved) at
  OrderId orderId;
  std::uint32_t clientRef;
  std::uint32_t clientSeq; // the submitting agent's own sequence number
  Quantity quantity;
  OrderType orderType;
  Side side;
  ExecutionType type;
};
static_assert(sizeof(TradeInfo) == 40, "TradeInfo should stay one cache line");

class Trade {
public:
  Trade(const TradeInfo &askTrade, const TradeInfo &bidTrade)
      : askTrade_(askTrade), bidTrade_(bidTrade) {};

  const TradeInfo &GetAskTrade() const { return askTrade_; }
  const TradeInfo &GetBidTrade() const { return bidTrade_; }

private:
  TradeInfo askTrade_;
  TradeInfo bidTrade_;
};
