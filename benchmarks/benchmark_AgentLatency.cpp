#include "Agent.h"
#include "AgentManager.h"
#include "AgentStrategy.h"
#include "AgentStrategyFactory.h"
#include "OrderPool.h"
#include "Orderbook.h"
#include "TradeDispatcher.h"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <thread>

#include <pthread.h>
#include <sched.h>
#include <stdio.h>

int main() {
  for (size_t n = 16; n <= 256; n *= 2) {
    TradeDispatcher tradeDispatcher;
    OrderPool orderPool;
    Orderbook orderbook(&orderPool, tradeDispatcher);
    MatchingEngine matchingEngine(orderbook, &orderPool);
    std::uint64_t maxTime{100'000};
    AgentManager agentManager_(maxTime);
    size_t nRandom = n;
    size_t nMarketMaker = n;
    size_t nMomentumTrader = n;

    double randomRate{1}, marketMakerRate{1}, momentumTraderRate{1};

    for (size_t i = 0; i < nRandom; ++i) {
      agentManager_.AddAgent(std::make_unique<Agent>(
          tradeDispatcher, matchingEngine,
          MakeStrategyRandom(&orderbook, &orderPool, 1),
          i + (nRandom + nMarketMaker), momentumTraderRate));
    }

    for (size_t i = 0; i < nMarketMaker; ++i) {
      agentManager_.AddAgent(std::make_unique<Agent>(
          tradeDispatcher, matchingEngine,
          MakeStrategyMarketMaker(&orderbook, &orderPool, 0.02),
          i + (nRandom + nMarketMaker), momentumTraderRate));
    }

    for (size_t i = 0; i < nMomentumTrader; ++i) {
      agentManager_.AddAgent(std::make_unique<Agent>(
          tradeDispatcher, matchingEngine,
          MakeStrategyMomentumTrader(&orderbook, &orderPool, 0.005),
          i + (nRandom + nMarketMaker), momentumTraderRate));
    }

    agentManager_.WarmUp();
    agentManager_.SetRunning(true);

    std::thread t1(&MatchingEngine::Start, &matchingEngine);
    std::thread t2(&AgentManager::RunIncomingLoop, &agentManager_);
    auto loop_start = std::chrono::steady_clock::now();
    agentManager_.RunOutgoingLoop();

    agentManager_.SetRunning(false);
    matchingEngine.Stop();
    auto loop_end = std::chrono::steady_clock::now();

    if (t1.joinable()) {
      t1.join();
    }

    if (t2.joinable()) {
      t2.join();
    }

    double duration_ms =
        std::chrono::duration<double, std::milli>(loop_end - loop_start)
            .count();
    double throughput_orders_per_sec =
        (agentManager_.GetNAgentActions() / duration_ms) * 1000.0;

    std::cout << "+---------------------------------------+" << std::endl;
    std::cout << "| Number of agents: 3x" << std::setw(10) << n << std::endl;
    std::cout << "| Actions processed: " << std::setw(10)
              << agentManager_.GetNAgentActions() << std::endl;
    std::cout << "| Orders processed: " << std::setw(10)
              << matchingEngine.GetProcessedOrders() << std::endl;
    std::cout << "| Duration: " << std::setw(12) << std::fixed
              << std::setprecision(2) << duration_ms << " ms" << std::endl;
    std::cout << "| Throughput (actions/s): " << std::setw(10) << std::fixed
              << std::setprecision(0) << throughput_orders_per_sec << " ops/sec"
              << std::endl;
  }
}
