#include "AgentManager.h"
#include "AgentStrategy.h"
#include "Format.h"
#include "MatchingEngine.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <numeric>
#include <unistd.h>
#include <x86intrin.h>

AgentManager::AgentManager(std::uint64_t maxTime) : maxTime_(maxTime) {};

void AgentManager::SetRunning(bool running) { running_ = running; }

void AgentManager::AddAgent(std::unique_ptr<Agent> agent) {
  agents_.push_back(std::move(agent));
}

void AgentManager::PushAgentEvent(AgentEvent &&event) {
  agentEventQueue_.Push(event);
}

void AgentManager::WarmUp() {
  agentEventQueue_.Reserve(agents_.size());
  for (size_t i = 0; i < agents_.size(); ++i) {
    double time = agents_[i]->ScheduleNextAction(currentTime_);
    size_t pos = i;
    PushAgentEvent(AgentEvent(time, pos));
  }
}

void AgentManager::RunOutgoingLoop() {
  if (agents_.empty()) {
    return;
  }

  AgentEvent event;
  double nextTime;
  while (currentTime_ < maxTime_) {
    if (agentEventQueue_.Empty()) {
      return;
    }
    event = agentEventQueue_.Top();
    OrderPtrs orders{agents_[event.pos]->Act()};
    ++agentActions_;
    for (auto &order : orders) {
      agents_[event.pos]->PushOrder(std::move(order));
    }
    currentTime_ = event.time;
    nextTime = agents_[event.pos]->ScheduleNextAction(currentTime_);
    // The acting agent is always the queue head, so one sift-down replaces
    // the pop+push pair.
    agentEventQueue_.ReplaceTop(AgentEvent(nextTime, event.pos));
  }
}

void AgentManager::RunIncomingLoop() {
  // Adaptive backoff: polling empty ring buffers hammers cache lines the
  // matching thread writes, so idle passes back off exponentially (capped).
  int idlePasses = 0;
  while (running_) {
    bool drainedAny = false;
    for (auto &agent : agents_) {
      // Drain each agent fully per pass instead of one trade per scan.
      while (agent->PopTrade()) {
        drainedAny = true;
      }
    }
    if (drainedAny) {
      idlePasses = 0;
    } else if (idlePasses < 64) {
      ++idlePasses;
    }
    for (int i = 0; i <= idlePasses; ++i) {
      _mm_pause();
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
    std::vector<double> unitsDelta;
    std::vector<double> profit;
  };
  static constexpr const char *STRATEGY_NAMES[] = {
      "Random", "Market Maker", "Momentum", "Mean Reverter", "Whale"};
  AgentData data[5];

  for (const auto &agent : agents_) {
    AgentInfo info = agent->GetInfo();
    const double totalCash =
        (agent->GetAvailableCash() + agent->GetReservedCash()) / 100.0;
    AgentData &bucket = data[static_cast<std::size_t>(info.strategy_)];
    bucket.profit.push_back(totalCash - agent->GetCash() / 100.0);
    bucket.unitsDelta.push_back(
        static_cast<double>(agent->GetUnits() - agent->GetInitialUnits()));
  }

  struct Row {
    const char *name;
    std::size_t n;
    double meanProfit, sigmaProfit, meanUnitsDelta, sigmaUnits;
  };
  std::vector<Row> rows;
  for (std::size_t i = 0; i < 5; ++i) {
    if (data[i].profit.empty()) {
      continue;
    }
    rows.push_back({STRATEGY_NAMES[i], data[i].profit.size(),
                    calculateMean(data[i].profit),
                    calculateStdDev(data[i].profit),
                    calculateMean(data[i].unitsDelta),
                    calculateStdDev(data[i].unitsDelta)});
  }
  // Leaderboard order: winners first.
  std::sort(rows.begin(), rows.end(),
            [](const Row &a, const Row &b) { return a.meanProfit > b.meanProfit; });

  const bool color = isatty(fileno(stdout)) != 0;
  const char *RED = color ? "\033[31m" : "";
  const char *GREEN = color ? "\033[32m" : "";
  const char *DIM = color ? "\033[90m" : "";
  const char *RESET = color ? "\033[0m" : "";

  // Whole pounds: pence are noise at these magnitudes and widen every column.
  const auto money = [](double value) {
    const std::int64_t pounds = std::llround(value < 0 ? -value : value);
    return std::string(value < 0 ? "-£" : "£") + WithCommas(pounds);
  };
  const auto signedCount = [](double value) {
    const std::int64_t count = std::llround(value);
    return (count > 0 ? "+" : "") + WithCommas(count);
  };

  double maxAbsProfit = 0;
  for (const Row &row : rows) {
    maxAbsProfit = std::max(maxAbsProfit, std::abs(row.meanProfit));
  }

  constexpr int BAR_WIDTH = 12;
  std::cout << "────────────────────── AGENT P&L ──────────────────────\n";
  // setw counts bytes: £, σ and Δ are 2 UTF-8 bytes, so widths below are
  // padded by one per symbol to keep the visual columns aligned.
  std::cout << DIM << " " << std::left << std::setw(15) << "type" << std::right
            << std::setw(4) << "n" << std::setw(13) << "mean P&L"
            << std::setw(13) << "σ P&L" << std::setw(12) << "units Δ"
            << std::setw(11) << "σ units" << "  mean P&L" << '\n' << RESET;
  for (const Row &row : rows) {
    const char *pnlColor = row.meanProfit > 0.005   ? GREEN
                           : row.meanProfit < -0.005 ? RED
                                                     : DIM;
    std::cout << " " << std::left << std::setw(15) << row.name << std::right
              << std::setw(4) << row.n << pnlColor << std::setw(14)
              << money(row.meanProfit) << RESET << std::setw(13)
              << money(row.sigmaProfit) << std::setw(11)
              << signedCount(row.meanUnitsDelta) << std::setw(10)
              << WithCommas(std::llround(row.sigmaUnits)) << "  " << pnlColor;
    // Eighth-block resolution keeps the bars linear across the whole range:
    // £1k next to £1M reads as a hairline next to a full bar, not 1 vs 12.
    static constexpr const char *EIGHTHS[] = {"", "▏", "▎", "▍",
                                              "▌", "▋", "▊", "▉"};
    int eighths =
        maxAbsProfit > 0
            ? static_cast<int>(std::abs(row.meanProfit) / maxAbsProfit *
                                   BAR_WIDTH * 8 + 0.5)
            : 0;
    if (eighths == 0 && std::abs(row.meanProfit) > 0.005) {
      eighths = 1; // nonzero stays visible as the thinnest sliver
    }
    for (int j = 0; j < eighths / 8; ++j) {
      std::cout << "█";
    }
    std::cout << EIGHTHS[eighths % 8] << RESET << '\n';
  }
  std::cout << '\n';
}
