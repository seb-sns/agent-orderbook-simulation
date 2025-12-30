#include "Agent.h"
#include "AgentManager.h"
#include "AgentStrategy.h"
#include "AgentStrategyFactory.h"
#include "OrderPool.h"
#include "Orderbook.h"
#include "TradeDispatcher.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

int main() {

  std::size_t nRandom;
  double sigma;
  double randomRate;
  while (true) {
    std::cout << "Enter how many Random agents you would like: ";
    std::cin >> nRandom;
    std::cout << "Enter the standard deviation of the price Random agents will "
                 "place orders at: ";
    std::cin >> sigma;
    std::cout << "Enter the average rate at which the Random agents will act "
                 "(time units/action): ";
    std::cin >> randomRate;
    std::cout << '\n';
    if (nRandom >= 0 && sigma > 0 && randomRate > 0) {
      break;
    } else {
      std::cout << "Must have 0 or more agents. Agent parameters must be "
                   "greater than 0"
                << '\n';
    }
  }

  std::size_t nMarketMaker;
  double marketMakerRate;
  while (true) {
    std::cout << "Enter how many Market Maker agents you would like: ";
    std::cin >> nMarketMaker;
    std::cout << "Enter the average rate at which the Market Maker agents will "
                 "act (time units/action): ";
    std::cin >> marketMakerRate;
    std::cout << '\n';
    if (nMarketMaker >= 0 && marketMakerRate > 0) {
      break;
    } else {
      std::cout << "Must have 0 or more agents. Agent parameters must be "
                   "greater than 0"
                << '\n';
    }
  }

  std::size_t nMomentumTrader;
  double momentumTraderRate;
  while (true) {
    std::cout << "Enter how many Momentum Trader agents you would like: ";
    std::cin >> nMomentumTrader;
    std::cout << "Enter the average rate at which the Momentum Trader agents "
                 "will act (time units/action): ";
    std::cin >> momentumTraderRate;
    std::cout << '\n';
    if (nMomentumTrader >= 0 && momentumTraderRate > 0) {
      break;
    } else {
      std::cout << "Must have 0 or more agents. Agent parameters must be "
                   "greater than 0"
                << '\n';
    }
  }

  std::uint64_t maxTime;
  while (true) {
    std::cout << "Enter how many time units you would like the simulation to "
                 "run for (MAX: 1'000'000'000): ";
    std::cin >> maxTime;
    std::cout << '\n';
    if (maxTime <= 1'000'000'000) {
      std::cout << '\n';
      break;
    } else {
      std::cout << "Exceeded limit" << '\n';
    }
  }

  TradeDispatcher tradeDispatcher;
  OrderPool orderPool;
  Orderbook orderbook(&orderPool, tradeDispatcher);
  MatchingEngine matchingEngine(orderbook, &orderPool);

  AgentManager agentManager_(maxTime);

  for (size_t i = 0; i < nRandom; ++i) {
    agentManager_.AddAgent(std::make_unique<Agent>(
        tradeDispatcher, matchingEngine, MakeStrategyRandom(&orderbook, &orderPool, sigma), i, randomRate));
  }

  for (size_t i = 0; i < nMarketMaker; ++i) {
    agentManager_.AddAgent(
        std::make_unique<Agent>(tradeDispatcher, matchingEngine, MakeStrategyMarketMaker(&orderbook, &orderPool, 0.02),
                                i + nRandom, marketMakerRate));
  }

  for (size_t i = 0; i < nMomentumTrader; ++i) {
    agentManager_.AddAgent(std::make_unique<Agent>(
        tradeDispatcher, matchingEngine, MakeStrategyMomentumTrader(&orderbook, &orderPool, 0.005),
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

  orderbook.PrintBook();
  // agentManager_.PrintStates();
  agentManager_.PrintSummary();
  return 0;
}

  // 
 


  // 
 
 

  // 
 


  // 
 
 
