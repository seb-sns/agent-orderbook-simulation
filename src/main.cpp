#include "Agent.h"
#include "AgentManager.h"
#include "AgentStrategy.h"
#include "AgentStrategyFactory.h"
#include "Format.h"
#include "OrderPool.h"
#include "Orderbook.h"
#include "TradeDispatcher.h"
#include "VizRecorder.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace {
// Line-based prompt with a default: Enter (or EOF, for scripted runs) accepts
// the default shown in brackets; invalid input re-prompts.
template <typename T, typename Pred>
T prompt(const std::string &message, T defaultValue, Pred validate,
         const std::string &errorMessage) {
  std::string line;
  while (true) {
    std::cout << message << " [" << defaultValue << "]: ";
    if (!std::getline(std::cin, line)) {
      std::cout << defaultValue << '\n';
      return defaultValue;
    }
    if (line.empty()) {
      return defaultValue;
    }
    std::istringstream parser(line);
    T value;
    if (parser >> value) {
      if (validate(value)) {
        return value;
      }
      std::cout << errorMessage << '\n';
      continue;
    }
    std::cout << "Invalid input. Please enter a valid value.\n";
  }
}

template <typename T> T prompt(const std::string &message, T defaultValue) {
  return prompt(
      message, defaultValue, [](const T &) { return true; }, "");
}
} // namespace

int main() {
  constexpr std::uint64_t MAX_SIMULATION_TIME = 1'000'000'000;
  constexpr double MARKET_MAKER_SPREAD = 0.02;
  constexpr double MOMENTUM_TRADER_THRESHOLD = 0.001; // 0.1% MA divergence
  constexpr double MEAN_REVERTER_FAIR_VALUE = 110.0;
  constexpr double MEAN_REVERTER_BAND = 0.25;
  constexpr Quantity WHALE_ORDER_SIZE = 100;

  const auto positive = [](double v) { return v > 0; };
  const char *positiveError = "Agent parameters must be greater than 0";
  const auto section = [](const char *title) {
    std::cout << '\n' << title << '\n';
  };

  std::cout << "──────────────── ORDERBOOK SIMULATION ─────────────────\n"
               "Press Enter to accept the [default] for any question.\n";

  section("Random agents — noise traders, limit orders around mid");
  std::size_t nRandom = prompt<std::size_t>("  count", 40);
  double sigma = prompt<double>("  price std-dev around mid (£)", 0.5,
                                positive, positiveError);
  double randomInterval = prompt<double>("  action interval (time units)", 2.0,
                                         positive, positiveError);

  section("Market Maker agents — quote both sides of the touch");
  std::size_t nMarketMaker = prompt<std::size_t>("  count", 10);
  double marketMakerInterval = prompt<double>(
      "  action interval (time units)", 2.0, positive, positiveError);

  section("Momentum Trader agents — chase moving-average crossovers");
  std::size_t nMomentumTrader = prompt<std::size_t>("  count", 5);
  double momentumTraderInterval = prompt<double>(
      "  action interval (time units)", 2.0, positive, positiveError);

  section("Mean Reverter agents — fade deviations from fair value");
  std::size_t nMeanReverter = prompt<std::size_t>("  count", 5);
  double meanReverterInterval = prompt<double>(
      "  action interval (time units)", 3.0, positive, positiveError);

  section("Whale agents — rare, large market orders");
  std::size_t nWhale = prompt<std::size_t>("  count", 2);
  double whaleInterval = prompt<double>("  action interval (time units)", 40.0,
                                        positive, positiveError);

  section("Run");
  std::uint64_t maxTime = prompt<std::uint64_t>(
      "  simulation length (time units)", std::uint64_t{20'000},
      [](std::uint64_t v) { return v > 0 && v <= MAX_SIMULATION_TIME; },
      "Must be between 1 and 1'000'000'000");

  const std::size_t nAgents =
      nRandom + nMarketMaker + nMomentumTrader + nMeanReverter + nWhale;
  std::cout << "\nRunning " << nAgents << " agents for "
            << WithCommas(static_cast<std::int64_t>(maxTime))
            << " time units...\n";

  TradeDispatcher tradeDispatcher;
  OrderPool orderPool;
  Orderbook orderbook(&orderPool, tradeDispatcher);
  MatchingEngine matchingEngine(orderbook, &orderPool);

  AgentManager agentManager_(maxTime);

  for (size_t i = 0; i < nRandom; ++i) {
    agentManager_.AddAgent(std::make_unique<Agent>(
        tradeDispatcher, matchingEngine,
        MakeStrategyRandom(&orderbook, &orderPool, sigma), i, randomInterval));
  }

  for (size_t i = 0; i < nMarketMaker; ++i) {
    agentManager_.AddAgent(std::make_unique<Agent>(
        tradeDispatcher, matchingEngine,
        MakeStrategyMarketMaker(&orderbook, &orderPool, MARKET_MAKER_SPREAD),
        i + nRandom, marketMakerInterval));
  }

  for (size_t i = 0; i < nMomentumTrader; ++i) {
    agentManager_.AddAgent(std::make_unique<Agent>(
        tradeDispatcher, matchingEngine,
        MakeStrategyMomentumTrader(&orderbook, &orderPool,
                                   MOMENTUM_TRADER_THRESHOLD),
        i + (nRandom + nMarketMaker), momentumTraderInterval));
  }

  const size_t meanReverterBase = nRandom + nMarketMaker + nMomentumTrader;
  for (size_t i = 0; i < nMeanReverter; ++i) {
    agentManager_.AddAgent(std::make_unique<Agent>(
        tradeDispatcher, matchingEngine,
        MakeStrategyMeanReverter(&orderbook, &orderPool,
                                 MEAN_REVERTER_FAIR_VALUE, MEAN_REVERTER_BAND),
        i + meanReverterBase, meanReverterInterval));
  }

  const size_t whaleBase = meanReverterBase + nMeanReverter;
  for (size_t i = 0; i < nWhale; ++i) {
    agentManager_.AddAgent(std::make_unique<Agent>(
        tradeDispatcher, matchingEngine,
        MakeStrategyWhale(&orderbook, &orderPool, WHALE_ORDER_SIZE),
        i + whaleBase, whaleInterval));
  }

#ifdef ENABLE_VIZ
  {
    VizRecorder &recorder = VizRecorder::Get();
    for (size_t i = 0; i < nRandom; ++i) {
      recorder.AddAgent(i, 0);
    }
    for (size_t i = 0; i < nMarketMaker; ++i) {
      recorder.AddAgent(i + nRandom, 1);
    }
    for (size_t i = 0; i < nMomentumTrader; ++i) {
      recorder.AddAgent(i + nRandom + nMarketMaker, 2);
    }
    for (size_t i = 0; i < nMeanReverter; ++i) {
      recorder.AddAgent(i + nRandom + nMarketMaker + nMomentumTrader, 3);
    }
    for (size_t i = 0; i < nWhale; ++i) {
      recorder.AddAgent(
          i + nRandom + nMarketMaker + nMomentumTrader + nMeanReverter, 4);
    }
    // Initial cash/units mirror Agent's defaults; price grid mirrors the
    // orderbook's (min price 100, tick 0.01).
    if (!recorder.Start("simulation.mktviz", 1'000'000'000, 100'000, 100.0,
                        0.01)) {
      return 1;
    }
  }
#endif

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

#ifdef ENABLE_VIZ
  VizRecorder::Get().Stop();
  std::cout << "Viz log written to simulation.mktviz ("
            << VizRecorder::Get().GetRecorded() << " events, "
            << VizRecorder::Get().GetDropped()
            << " dropped). Open viz/viewer.html in a browser to replay.\n";
#endif

  std::cout << "Run complete: "
            << WithCommas(
                   static_cast<std::int64_t>(agentManager_.GetNAgentActions()))
            << " agent actions, "
            << WithCommas(static_cast<std::int64_t>(
                   matchingEngine.GetProcessedOrders()))
            << " orders processed.\n";

  orderbook.PrintBook();
  // agentManager_.PrintStates();
  agentManager_.PrintSummary();
  return 0;
}
