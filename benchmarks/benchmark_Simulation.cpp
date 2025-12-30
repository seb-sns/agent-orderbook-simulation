#include "Agent.h"
#include "AgentManager.h"
#include "AgentStrategy.h"
#include "AgentStrategyFactory.h"
#include "Orderbook.h"
#include "TradeDispatcher.h"

#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <thread>

static void BM_Simulation(benchmark::State &state) {
  for (auto _ : state) {
    TradeDispatcher tradeDispatcher;
    OrderPool orderPool;
    Orderbook orderbook(&orderPool, tradeDispatcher);
    MatchingEngine matchingEngine(orderbook, &orderPool);
    std::uint64_t maxTime{100'000};
    AgentManager agentManager_(maxTime);
    size_t nRandom = state.range(0);
    size_t nMarketMaker = state.range(0);
    size_t nMomentumTrader = state.range(0);

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

    agentManager_.RunOutgoingLoop();

    agentManager_.SetRunning(false);
    matchingEngine.Stop();

    if (t1.joinable()) {
      t1.join();
    }

    if (t2.joinable()) {
      t2.join();
    }
  }
}

BENCHMARK(BM_Simulation)->RangeMultiplier(2)->Range(16, 256);

BENCHMARK_MAIN();
