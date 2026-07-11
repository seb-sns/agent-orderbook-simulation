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
class MeanReverter;
class Whale;

using AgentStrategy =
    std::variant<MarketMaker, MomentumTrader, Random, MeanReverter, Whale>;

using OrderPtrs = std::vector<Order *>;

// Quotes both sides of the touch, cancel-and-replace each action. The quote
// width is the base spread plus a volatility premium (EWMA of squared mid
// moves between actions), and the quote center is skewed against inventory
// (reservation price) so quotes retreat from the side being run over; size
// scales down as the width scales up. Each side is quoted independently of
// whether the other is fundable.
class MarketMaker {
public:
  MarketMaker(Orderbook *orderbook, OrderPool *orderPool, double spread);

  MarketMaker(const MarketMaker &) = delete;
  MarketMaker &operator=(const MarketMaker &) = delete;

  MarketMaker(MarketMaker &&) = default;
  MarketMaker &operator=(MarketMaker &&) = default;

  OrderPtrs Act(Agent *agent);
  OrderPtrs CreateOrders(Agent *agent);
  OrderPtrs CancelOrders(Agent *agent);

private:
  double spread_;
  double lastMid_{-1.0}; // < 0: no mid observed yet
  double ewmaVar_{0.0};  // EWMA of squared mid moves between actions
  Orderbook *orderbook_;
  OrderPool *orderPool_;
};

// Trend follower: compares a short and a long moving average of the mid and
// holds a bounded position proportional to their divergence (time-series
// momentum) — building via capped market-order slices while the signal
// grows, holding while it persists, unwinding as it decays.
class MomentumTrader {
public:
  MomentumTrader(Orderbook *orderbook, OrderPool *orderPool, double threshold);

  MomentumTrader(const MomentumTrader &) = delete;
  MomentumTrader &operator=(const MomentumTrader &) = delete;

  MomentumTrader(MomentumTrader &&) = default;
  MomentumTrader &operator=(MomentumTrader &&) = default;

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

// Fundamentalist: believes the asset has a fair value and fades deviations —
// lifts the ask when the mid drops below fairValue - band, hits the bid when
// it rises above fairValue + band. Cancels resting orders once the mid is
// back inside the band (or its stance flips). Fair value itself drifts
// toward the mid by adaptRate per action (EWMA), so a persistent repricing
// is eventually accepted as the new fair value instead of being averaged
// into until the agent is broke; adaptRate 0 keeps it fixed.
class MeanReverter {
public:
  MeanReverter(Orderbook *orderbook, OrderPool *orderPool, double fairValue,
               double band, double adaptRate);

  MeanReverter(const MeanReverter &) = delete;
  MeanReverter &operator=(const MeanReverter &) = delete;

  MeanReverter(MeanReverter &&) = default;
  MeanReverter &operator=(MeanReverter &&) = default;

  OrderPtrs Act(Agent *agent);
  OrderPtrs CreateOrders(Agent *agent);
  OrderPtrs CancelOrders(Agent *agent);

private:
  double fairValue_;
  double band_;
  double adaptRate_;
  Orderbook *orderbook_;
  OrderPool *orderPool_;
};

// Liquidity taker: submits one large market order per action, random side.
// Meant to act rarely (long action interval) and shock the book.
class Whale {
public:
  Whale(Orderbook *orderbook, OrderPool *orderPool, Quantity orderSize);

  Whale(const Whale &) = delete;
  Whale &operator=(const Whale &) = delete;

  Whale(Whale &&) = default;
  Whale &operator=(Whale &&) = default;

  OrderPtrs Act(Agent *agent);
  OrderPtrs CreateOrders(Agent *agent);
  OrderPtrs CancelOrders(Agent *agent);

private:
  std::bernoulli_distribution sideDistribution_;
  std::uniform_int_distribution<Quantity> quantityDistribution_;
  Orderbook *orderbook_;
  OrderPool *orderPool_;
};

class Random {
public:
  Random(Orderbook *orderbook, OrderPool *orderPool, double sigma);

  Random(const Random &) = delete;
  Random &operator=(const Random &) = delete;

  Random(Random &&) = default;
  Random &operator=(Random &&) = default;

  OrderPtrs Act(Agent *agent);
  OrderPtrs CreateOrders(Agent *agent);
  OrderPtrs CancelOrders(Agent *agent);

private:
  double sigma_;
  std::normal_distribution<double> normal_distribution_;
  std::bernoulli_distribution bernoulli_distribution_;
  std::uniform_int_distribution<Quantity> quantityDistribution_;
  Orderbook *orderbook_;
  OrderPool *orderPool_;
};
