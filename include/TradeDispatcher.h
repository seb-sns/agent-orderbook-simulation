#pragma once
#include "Order.h"
#include "Trade.h"
#include <vector>

class Agent;

// Routes TradeInfos from the matching thread to agents. Client refs are
// dense (0..N-1), so routing is a vector index instead of a hash lookup.
// Attach/Detach must happen before/after the matching thread runs.
class TradeDispatcher {
public:
  TradeDispatcher() {};

  void Attach(Agent *agent);
  void Detach(Agent *agent);
  void PushTradeInfo(Trade &&trade);
  void PushTradeInfo(TradeInfo &&tradeInfo);

private:
  Agent *ClientFor(ClientRef clientRef) const {
    return clientRef < clients_.size() ? clients_[clientRef] : nullptr;
  }
  std::vector<Agent *> clients_;
};
