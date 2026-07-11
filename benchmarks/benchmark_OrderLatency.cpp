#include "Agent.h"
#include "AgentStrategy.h"
#include "MatchingEngine.h"
#include "Order.h"
#include "Orderbook.h"
#include "ThreadPin.h"
#include "TradeDispatcher.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <vector>

// Streams orders through MatchingEngine::ProcessOrder one at a time, timing
// each call. Orders are allocated just before processing so the working set
// fits the pool (a resting book plus in-flight orders), matching how the real
// simulation uses it. ~5% of iterations first cancel a randomly chosen recent
// resting limit order (by its real order id, so cancels genuinely exercise
// the erase path — some will miss because the target already filled, which is
// realistic).
int main() {
  PinCurrentThreadToCore(2);
  constexpr int N_ORDERS = 5'000'000;

  std::bernoulli_distribution bernoulli_distribution(0.5);
  std::bernoulli_distribution bernoulli_distribution_cancel(0.05);
  std::normal_distribution<> normal_distribution(0, 5);
  std::random_device rd;
  std::mt19937 gen(rd());
  TradeDispatcher tradeDispatcher;
  OrderPool orderPool;
  Orderbook orderbook(&orderPool, tradeDispatcher);
  MatchingEngine matchingEngine(orderbook, &orderPool);

  std::unique_ptr<Agent> agent =
      std::make_unique<Agent>(tradeDispatcher, matchingEngine,
                              Random(&orderbook, &orderPool, 0.5), 0, 1);

  struct RecentOrder {
    OrderId id;
    Side side;
    Price price;
  };
  std::array<RecentOrder, 128> recent;
  std::size_t recentCount = 0, recentHead = 0;

  std::vector<double> latencies;
  latencies.reserve(N_ORDERS + N_ORDERS / 16);

  auto processTimed = [&](Order *order) {
    auto start = std::chrono::steady_clock::now();
    matchingEngine.ProcessOrder(order);
    auto end = std::chrono::steady_clock::now();
    latencies.push_back(
        std::chrono::duration<double, std::nano>(end - start).count());
  };

  auto loop_start = std::chrono::steady_clock::now();
  for (int i = 0; i < N_ORDERS; ++i) {
    if (recentCount > 0 && bernoulli_distribution_cancel(gen)) {
      const RecentOrder &target =
          recent[(recentHead + gen() % recentCount) % recent.size()];
      PoolIndex index = orderPool.allocate();
      Order *cancelOrder = orderPool.get_order(index);
      cancelOrder->SetOrderId(target.id);
      cancelOrder->SetOrderType(OrderType::CANCEL);
      cancelOrder->SetClientRef(0);
      cancelOrder->SetSide(target.side);
      cancelOrder->SetPrice(target.price);
      cancelOrder->SetInitialQuantity(0);
      cancelOrder->SetRemainingQuantity(0);
      cancelOrder->SetIndex(index);
      processTimed(cancelOrder);
    }

    Price price = 110 + normal_distribution(gen);
    price = std::round(price * 100.0) / 100.0;
    const Quantity quantity = static_cast<Quantity>(
        std::clamp(std::round(10 + normal_distribution(gen)), 1.0, 20.0));
    const Side side = bernoulli_distribution(gen) ? Side::Buy : Side::Sell;
    const OrderType orderType =
        bernoulli_distribution(gen) ? OrderType::LIMIT : OrderType::MARKET;

    PoolIndex index = orderPool.allocate();
    Order *order = orderPool.get_order(index);
    order->SetOrderId(i + 1);
    order->SetOrderType(orderType);
    order->SetClientRef(0);
    order->SetSide(side);
    order->SetPrice(price);
    order->SetInitialQuantity(quantity);
    order->SetRemainingQuantity(quantity);
    order->SetIndex(index);

    if (orderType == OrderType::LIMIT) {
      if (recentCount < recent.size()) {
        recent[(recentHead + recentCount++) % recent.size()] = {
            static_cast<OrderId>(i + 1), side, price};
      } else {
        recent[recentHead] = {static_cast<OrderId>(i + 1), side, price};
        recentHead = (recentHead + 1) % recent.size();
      }
    }
    processTimed(order);

    // Drain the agent's incoming trade buffer between timed calls — nothing
    // else consumes it in this benchmark and PushTrade spins when it fills.
    agent->ClearIncoming();
  }
  auto loop_end = std::chrono::steady_clock::now();

  std::sort(latencies.begin(), latencies.end());
  double total_operations = latencies.size();
  double duration_ms =
      std::chrono::duration<double, std::milli>(loop_end - loop_start).count();
  double throughput_ops_per_sec = (total_operations / duration_ms) * 1000.0;

  double avg_latency_ns;
  double p50_latency_ns;
  double p95_latency_ns;
  double p99_latency_ns;
  double p999_latency_ns;
  double p9999_latency_ns;
  double max_latency_ns;

  if (!latencies.empty()) {
    avg_latency_ns = std::accumulate(latencies.begin(), latencies.end(), 0.0) /
                     latencies.size();
    p50_latency_ns = latencies[latencies.size() * 50 / 100];
    p95_latency_ns = latencies[latencies.size() * 95 / 100];
    p99_latency_ns = latencies[latencies.size() * 99 / 100];
    p999_latency_ns = latencies[latencies.size() * 999 / 1000];
    p9999_latency_ns = latencies[latencies.size() * 9999 / 10000];
    max_latency_ns = latencies.back();
  }

  std::cout << "+---------------------------------------+" << std::endl;
  std::cout << "| Orders processed: " << std::setw(10) << std::fixed
            << std::setprecision(1) << total_operations << std::endl;
  std::cout << "| Duration: " << std::setw(12) << std::fixed
            << std::setprecision(2) << duration_ms << " ms" << std::endl;
  std::cout << "| Throughput: " << std::setw(10) << std::fixed
            << std::setprecision(0) << throughput_ops_per_sec << " ops/sec"
            << std::endl;
  std::cout << "| Average latency: " << std::setw(10) << avg_latency_ns
            << " ns" << std::endl;
  std::cout << "| Median latency: " << std::setw(10) << p50_latency_ns << " ns"
            << std::endl;
  std::cout << "| 95th percentile: " << std::setw(10) << p95_latency_ns
            << " ns" << std::endl;
  std::cout << "| 99th percentile: " << std::setw(10) << p99_latency_ns
            << " ns" << std::endl;
  std::cout << "| 99.9th percentile: " << std::setw(10) << p999_latency_ns
            << " ns" << std::endl;
  std::cout << "| 99.99th percentile: " << std::setw(10) << p9999_latency_ns
            << " ns" << std::endl;
  std::cout << "| Max latency: " << std::setw(10) << max_latency_ns << " ns"
            << std::endl;
  std::cout << "+---------------------------------------+" << std::endl;
  return 0;
}
