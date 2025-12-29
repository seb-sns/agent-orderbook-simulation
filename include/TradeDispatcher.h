#pragma once
#include "Order.h"
#include "Trade.h"
#include <unordered_map>

class Agent;

class TradeDispatcher {
public:
  TradeDispatcher() {};

  void Attach(Agent *agent);
  void Detach(Agent *agent);
  void PushTradeInfo(Trade &&trade);

private:
  std::unordered_map<ClientRef, Agent *> clients_;
};
