#pragma once

#include "Order.h"

enum class ExecutionType { CANCEL, PARTIAL, FULL, INVALID };

// Trade info gives information on the trade as well as providing the original
// order from the opposite side

struct TradeInfo {
  OrderId orderId;
  OrderType orderType;
  ClientRef clientRef;
  Side side;
  Price price;
  Quantity quantity;
  Order order;
  ExecutionType type;
};

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
