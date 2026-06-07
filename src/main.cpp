#include "Agent.h"
#include "AgentManager.h"
#include "AgentStrategy.h"
#include "AgentStrategyFactory.h"
#include "OrderPool.h"
#include "Orderbook.h"
#include "TradeDispatcher.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <thread>

namespace {
template <typename T> T prompt(const std::string &message) {
  T value;
  while (true) {
    std::cout << message;
    std::cin >> value;
    if (!std::cin.fail())
      return value;
    if (std::cin.eof()) {
      std::cerr << "Unexpected end of input.\n";
      std::exit(1);
    }
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cout << "Invalid input. Please enter a valid value.\n";
  }
}

template <typename T, typename Pred>
T prompt(const std::string &message, Pred validate,
         const std::string &errorMessage) {
  T value;
  while (true) {
    std::cout << message;
    std::cin >> value;
    if (std::cin.fail()) {
      if (std::cin.eof()) {
        std::cerr << "Unexpected end of input.\n";
        std::exit(1);
      }
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      std::cout << "Invalid input. Please enter a valid value.\n";
      continue;
    }
    if (validate(value)) {
      return value;
    }
    std::cout << errorMessage << '\n';
  }
}
} // namespace

int main() {
  constexpr std::uint64_t MAX_SIMULATION_TIME = 1'000'000'000;
  constexpr double MARKET_MAKER_SPREAD = 0.02;
  constexpr double MOMENTUM_TRADER_THRESHOLD = 0.005;

  std::size_t nRandom =
      prompt<std::size_t>("Enter how many Random agents you would like: ");
  double sigma = prompt<double>(
      "Enter the standard deviation of the price Random agents will "
      "place orders at: ",
      [](double v) { return v > 0; },
      "Agent parameters must be greater than 0");
  double randomRate = prompt<double>(
      "Enter the average rate at which the Random agents will act "
      "(time units/action): ",
      [](double v) { return v > 0; },
      "Agent parameters must be greater than 0");

  std::size_t nMarketMaker = prompt<std::size_t>(
      "Enter how many Market Maker agents you would like: ");
  double marketMakerRate = prompt<double>(
      "Enter the average rate at which the Market Maker agents will "
      "act (time units/action): ",
      [](double v) { return v > 0; },
      "Agent parameters must be greater than 0");

  std::size_t nMomentumTrader = prompt<std::size_t>(
      "Enter how many Momentum Trader agents you would like: ");
  double momentumTraderRate = prompt<double>(
      "Enter the average rate at which the Momentum Trader agents "
      "will act (time units/action): ",
      [](double v) { return v > 0; },
      "Agent parameters must be greater than 0");

  std::uint64_t maxTime = prompt<std::uint64_t>(
      "Enter how many time units you would like the simulation to "
      "run for (MAX: 1'000'000'000): ",
      [](std::uint64_t v) { return v <= MAX_SIMULATION_TIME; },
      "Exceeded limit");
  std::cout << '\n';

  TradeDispatcher tradeDispatcher;
  OrderPool orderPool;
  Orderbook orderbook(&orderPool, tradeDispatcher);
  MatchingEngine matchingEngine(orderbook, &orderPool);

  AgentManager agentManager_(maxTime);

  for (size_t i = 0; i < nRandom; ++i) {
    agentManager_.AddAgent(std::make_unique<Agent>(
        tradeDispatcher, matchingEngine,
        MakeStrategyRandom(&orderbook, &orderPool, sigma), i, randomRate));
  }

  for (size_t i = 0; i < nMarketMaker; ++i) {
    agentManager_.AddAgent(std::make_unique<Agent>(
        tradeDispatcher, matchingEngine,
        MakeStrategyMarketMaker(&orderbook, &orderPool, MARKET_MAKER_SPREAD),
        i + nRandom, marketMakerRate));
  }

  for (size_t i = 0; i < nMomentumTrader; ++i) {
    agentManager_.AddAgent(std::make_unique<Agent>(
        tradeDispatcher, matchingEngine,
        MakeStrategyMomentumTrader(&orderbook, &orderPool,
                                   MOMENTUM_TRADER_THRESHOLD),
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
