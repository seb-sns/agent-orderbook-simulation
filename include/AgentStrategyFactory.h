#pragma once
#include "AgentStrategy.h"

AgentStrategy MakeStrategyRandom(Orderbook *orderbook, OrderPool *orderPool,
                                 double sigma) {
  return AgentStrategy{std::in_place_type<Random>, orderbook, orderPool, sigma};
}

AgentStrategy MakeStrategyMarketMaker(Orderbook *orderbook,
                                      OrderPool *orderPool, double spread) {
  return AgentStrategy{std::in_place_type<MarketMaker>, orderbook, orderPool,
                       spread};
}

AgentStrategy MakeStrategyMomentumTrader(Orderbook *orderbook,
                                         OrderPool *orderPool,
                                         double threshold) {
  return AgentStrategy{std::in_place_type<MomentumTrader>, orderbook, orderPool,
                       threshold};
}
