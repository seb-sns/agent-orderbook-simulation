#include "AgentManager.h"
#include "AgentStrategy.h"
#include "MatchingEngine.h"
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <numeric>

AgentManager::AgentManager(std::uint64_t maxTime) : maxTime_(maxTime) {};

void AgentManager::SetRunning(bool running) { running_ = running; }

void AgentManager::AddAgent(std::unique_ptr<Agent> agent) {
  agents_.push_back(std::move(agent));
}

void AgentManager::PushAgentEvent(AgentEvent &&event) {
  agentEventQueue_.Push(std::move(event));
}

void AgentManager::WarmUp() {
  for (size_t i = 0; i < agents_.size(); ++i) {
    double time = agents_[i]->ScheduleNextAction(currentTime_);
    size_t pos = i;
    PushAgentEvent(AgentEvent(time, pos));
  }
}

void AgentManager::RunOutgoingLoop() {
  AgentEvent event;
  double nextTime;
  while (currentTime_ < maxTime_) {
    agentEventQueue_.Pop(event);
    OrderPtrs orders{agents_[event.pos]->Act()};
    ++agentActions_;
    for (auto &order : orders) {
      agents_[event.pos]->PushOrder(std::move(order));
    }
    currentTime_ = event.time;
    nextTime = agents_[event.pos]->ScheduleNextAction(currentTime_);
    PushAgentEvent(AgentEvent(nextTime, event.pos));
  }
}

void AgentManager::RunIncomingLoop() {
  while (running_) {
    for (auto &agent : agents_) {
      agent->PopTrade();
    }
  }
  // Empty out each agent incoming buffer once we are done
  for (auto &agent : agents_) {
    agent->ClearIncoming();
  }
}

std::uint64_t AgentManager::GetNAgentActions() const { return agentActions_; }

void AgentManager::PrintStates() {
  for (auto &agent : agents_) {
    agent->PrintState();
  }
}

void AgentManager::PrintSummary() {
  auto calculateMean = [](const std::vector<double> &values) {
    if (values.empty())
      return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
  };

  auto calculateStdDev = [calculateMean](const std::vector<double> &values) {
    if (values.size() < 2)
      return 0.0; // Stdev is not defined for < 2 elements
    double mean = calculateMean(values);
    double sq_sum = std::inner_product(
        values.begin(), values.end(), values.begin(), 0.0,
        [](double a, double b) { return a + b; },
        [mean](double a, double b) { return (a - mean) * (b - mean); });
    return std::sqrt(sq_sum / static_cast<double>(values.size()));
  };

  struct AgentData {
    std::vector<double> cash;
    std::vector<double> units;
    std::vector<double> profit;
  };

  AgentData randomAgents, marketMakerAgents, momentumTraderAgents;
  constexpr std::int64_t initial_cash_per_agent = 1'000'000'000;

  for (const auto &agent : agents_) {
    AgentInfo info = agent->GetInfo();
    double totalCash =
        (agent->GetAvailableCash() + agent->GetReservedCash()) / 100.0;
    double units = agent->GetUnits();
    double profit = (totalCash) - (initial_cash_per_agent / 100.0);

    switch (info.strategy_) {
    case AgentInfo::Strategy::RANDOM:
      randomAgents.cash.push_back(totalCash);
      randomAgents.units.push_back(units);
      randomAgents.profit.push_back(profit);
      break;
    case AgentInfo::Strategy::MARKETMAKER:
      marketMakerAgents.cash.push_back(totalCash);
      marketMakerAgents.units.push_back(units);
      marketMakerAgents.profit.push_back(profit);
      break;
    case AgentInfo::Strategy::MOMENTUMTRADER:
      momentumTraderAgents.cash.push_back(totalCash);
      momentumTraderAgents.units.push_back(units);
      momentumTraderAgents.profit.push_back(profit);
      break;
    }
  }

  auto printAgentStats = [&](const std::string &agentName,
                             const AgentData &data) {
    const int LABEL_WIDTH = 40;
    const int VALUE_WIDTH = 15;

    double meanProfit = calculateMean(data.profit);
    double profitStdDev = calculateStdDev(data.profit);
    double meanCash = calculateMean(data.cash);
    double cashStdDev = calculateStdDev(data.cash);
    double meanUnits = calculateMean(data.units);
    double unitsStdDev = calculateStdDev(data.units);

    auto printLine = [&](const std::string &label, double value) {
      std::ostringstream line;
      line << std::fixed << std::setprecision(3) << std::left
           << std::setw(LABEL_WIDTH) << (label + ":") << std::right
           << std::setw(VALUE_WIDTH) << value;
      std::cout << line.str() << '\n';
    };

    printLine(agentName + " mean profit", meanProfit);
    printLine(agentName + " profit σ", profitStdDev);
    printLine(agentName + " mean cash", meanCash);
    printLine(agentName + " cash σ", cashStdDev);
    printLine(agentName + " mean units", meanUnits);
    printLine(agentName + " units σ", unitsStdDev);

    std::cout << "\n";
  };

  printAgentStats("Random Agents", randomAgents);
  printAgentStats("Market Maker Agents", marketMakerAgents);
  printAgentStats("Momentum Trader Agents", momentumTraderAgents);
}
