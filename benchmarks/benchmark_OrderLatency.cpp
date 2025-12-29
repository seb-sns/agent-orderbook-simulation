#include "Agent.h"
#include "AgentStrategy.h"
#include "MatchingEngine.h"
#include "Order.h"
#include "Orderbook.h"
#include "TradeDispatcher.h"

#include <chrono>
#include <memory>
#include <random>
#include <ratio>
#include <vector>

int main() {
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

  auto CreateRandomOrders = [&]() {
    std::vector<Order *> orders;
    for (int i = 0; i < 5'000'000; ++i) {
      bool cancel_result = bernoulli_distribution_cancel(gen);
      if (cancel_result && i > 100) {
        auto selectedIndex = std::rand() % 100 + 1;
        Order *selectedOrder = orders[i - selectedIndex];
        PoolIndex index = orderPool.allocate();
        Order *cancelOrder = orderPool.get_order(index);
        cancelOrder->SetOrderId(i);
        cancelOrder->SetOrderType(OrderType::CANCEL);
        cancelOrder->GetClientRef(0);
        cancelOrder->SetSide(selectedOrder->GetSide());
        cancelOrder->SetPrice(selectedOrder->GetPrice());
        cancelOrder->SetInitialQuantity(0);
        cancelOrder->SetRemainingQuantity(0);
        cancelOrder->SetIndex(index);

        orders.push_back(cancelOrder);
      }
      OrderType orderType;
      Side side;
      Price price = 110 + normal_distribution(gen);
      price = std::round(price * 100.0) / 100.0;
      Quantity quantity = 10 + normal_distribution(gen);
      quantity = std::round(quantity * 100.0) / 100.0;
      bool side_result = bernoulli_distribution(gen);
      if (side_result) {
        side = Side::Buy;
      } else {
        side = Side::Sell;
      }
      bool type_result = bernoulli_distribution(gen);
      if (type_result) {
        orderType = OrderType::LIMIT;
      } else {
        orderType = OrderType::MARKET;
      }
      PoolIndex index = orderPool.allocate();
      Order *order = orderPool.get_order(index);
      order->SetOrderId(i);
      order->SetOrderType(orderType);
      order->GetClientRef(0);
      order->SetSide(side);
      order->SetPrice(price);
      order->SetInitialQuantity(quantity);
      order->SetRemainingQuantity(quantity);
      order->SetIndex(index);

      orders.push_back(order);
    }
    return orders;
  };

  std::vector<Order *> orders = CreateRandomOrders();
  std::vector<double> latencies;
  latencies.reserve(10'000'000);

  auto loop_start = std::chrono::steady_clock::now();
  for (auto order : orders) {
    auto start = std::chrono::steady_clock::now();
    matchingEngine.ProcessOrder(std::move(order));
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::nano>(end - start).count();
    latencies.push_back(ms);
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

  if (max_latency_ns > 0 || p99_latency_ns > 0) {
    std::cout << "+---------------------------------------+" << std::endl;
    std::cout << "| Latency Statistics (nanoseconds)" << std::endl;
    std::cout << "|   Average: " << std::setw(10) << std::fixed
              << std::setprecision(1) << avg_latency_ns << " ns" << std::endl;
    std::cout << "|   50th %:  " << std::setw(10) << std::fixed
              << std::setprecision(1) << p50_latency_ns << " ns" << std::endl;
    std::cout << "|   95th %:  " << std::setw(10) << std::fixed
              << std::setprecision(1) << p95_latency_ns << " ns" << std::endl;
    std::cout << "|   99th %:  " << std::setw(10) << std::fixed
              << std::setprecision(1) << p99_latency_ns << " ns" << std::endl;
    std::cout << "|   99.9th %:" << std::setw(10) << std::fixed
              << std::setprecision(1) << p999_latency_ns << " ns" << std::endl;
    std::cout << "|   99.99th %:" << std::setw(9) << std::fixed
              << std::setprecision(1) << p9999_latency_ns << " ns" << std::endl;
    std::cout << "|   Max:     " << std::setw(10) << std::fixed
              << std::setprecision(1) << max_latency_ns << " ns" << std::endl;
  } else {
    std::cout << "| Avg Latency: " << std::setw(8) << std::fixed
              << std::setprecision(1) << avg_latency_ns << " ns" << std::endl;
  }

  std::cout << "+---------------------------------------+" << std::endl;
}
