#pragma once
#include "AgentStrategy.h"

inline AgentStrategy MakeStrategyRandom(Orderbook *orderbook, OrderPool *orderPool,
                                 double sigma) {
  return AgentStrategy{std::in_place_type<Random>, orderbook, orderPool, sigma};
}

inline AgentStrategy MakeStrategyMarketMaker(Orderbook *orderbook,
                                      OrderPool *orderPool, double spread) {
  return AgentStrategy{std::in_place_type<MarketMaker>, orderbook, orderPool,
                       spread};
}

inline AgentStrategy MakeStrategyMomentumTrader(Orderbook *orderbook,
                                         OrderPool *orderPool,
                                         double threshold) {
  return AgentStrategy{std::in_place_type<MomentumTrader>, orderbook, orderPool,
                       threshold};
}

inline AgentStrategy MakeStrategyMeanReverter(Orderbook *orderbook,
                                       OrderPool *orderPool, double fairValue,
                                       double band, double adaptRate) {
  return AgentStrategy{std::in_place_type<MeanReverter>, orderbook, orderPool,
                       fairValue, band, adaptRate};
}

inline AgentStrategy MakeStrategyWhale(Orderbook *orderbook, OrderPool *orderPool,
                                Quantity orderSize) {
  return AgentStrategy{std::in_place_type<Whale>, orderbook, orderPool,
                       orderSize};
}
