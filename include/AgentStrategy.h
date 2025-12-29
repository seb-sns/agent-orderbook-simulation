#pragma once

#include "FixedDeque.h"
#include "OrderPool.h"
#include "Orderbook.h"
#include <random>
#include <variant>

class Agent;

class MarketMaker;
class MomentumTrader;
class Random;

using AgentStrategy = std::variant<MarketMaker, MomentumTrader, Random>;

using OrderPtrs = std::vector<Order *>;

class MarketMaker {
public:
  MarketMaker(Orderbook *orderbook, OrderPool *orderPool, double spread);
  OrderPtrs Act(Agent *agent);
  OrderPtrs CreateOrders(Agent *agent);
  OrderPtrs CancelOrders(Agent *agent);

private:
  double spread_;
  double lastMidPrice_;
  double midPrice_;
  Orderbook *orderbook_;
  OrderPool *orderPool_;
};

class MomentumTrader {
public:
  MomentumTrader(Orderbook *orderbook, OrderPool *orderPool, double threshold);
  OrderPtrs Act(Agent *agent);
  OrderPtrs CreateOrders(Agent *agent);
  OrderPtrs CancelOrders(Agent *agent);

private:
  FixedDeque<double> longTermObservations_{256};
  FixedDeque<double> shortTermObservations_{32};

  double shortTermSum_{0};
  double longTermSum_{0};
  double threshold_;
  Orderbook *orderbook_;
  OrderPool *orderPool_;
};

class Random {
public:
  Random(Orderbook *orderbook, OrderPool *orderPool, double sigma);
  OrderPtrs Act(Agent *agent);
  OrderPtrs CreateOrders(Agent *agent);
  OrderPtrs CancelOrders(Agent *agent);

private:
  double sigma_;
  std::normal_distribution<double> normal_distribution_;
  std::bernoulli_distribution bernoulli_distribution_;
  Orderbook *orderbook_;
  OrderPool *orderPool_;
};
