#pragma once

#include "OrderPool.h"
#include "Orderbook.h"
#include "SingleThreadRingBuffer.h"
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

  MarketMaker(const MarketMaker&) = delete;
  MarketMaker& operator=(const MarketMaker&) = delete;

  MarketMaker(MarketMaker&&) = default;
  MarketMaker& operator=(MarketMaker&&) = default;

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

  MomentumTrader(const MomentumTrader&) = delete;
  MomentumTrader& operator=(const MomentumTrader&) = delete;

  MomentumTrader(MomentumTrader&&) = default;
  MomentumTrader& operator=(MomentumTrader&&) = default;

  OrderPtrs Act(Agent *agent);
  OrderPtrs CreateOrders(Agent *agent);
  OrderPtrs CancelOrders(Agent *agent);

private:
  SingleThreadRingBuffer<double, 256> longTermObservations_;
  SingleThreadRingBuffer<double, 32> shortTermObservations_;

  double shortTermSum_{0};
  double longTermSum_{0};
  double threshold_;
  Orderbook *orderbook_;
  OrderPool *orderPool_;
};

class Random {
public:
  Random(Orderbook *orderbook, OrderPool *orderPool, double sigma);

  Random(const Random&) = delete;
  Random& operator=(const Random&) = delete;

  Random(Random&&) = default;
  Random& operator=(Random&&) = default;

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
