#include "AgentManager.h"
#include "CalenderQueue.h"
#include "LadderQueue.h"
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <queue>

static std::mt19937 gen(42);
static std::uniform_real_distribution<> dis(0.0, 1.0);

double sampleExponential(double rate) {
  double U = dis(gen);
  return -std::log(U) / rate;
}

int main() {

  const size_t numEvents = 500'000;

  // Example queue (replace with your queue type)
  std::priority_queue<AgentEvent> agentEventQueue;

  // Warm-up: fill a few events to stabilize allocations
  for (size_t i = 0; i < 10'000; ++i) {
    AgentEvent e{sampleExponential(1.0), 0};
    agentEventQueue.push(std::move(e));
  }

  std::vector<double> pushLatencies;
  pushLatencies.reserve(numEvents);

  // --- Batch timing for pushes ---
  auto pushStart = std::chrono::steady_clock::now();
  for (size_t i = 0; i < numEvents; ++i) {
    auto eventStart = std::chrono::steady_clock::now();

    AgentEvent e;
    e.time = sampleExponential(1.0);
    e.pos = 0;

    agentEventQueue.push(std::move(e));

    auto eventEnd = std::chrono::steady_clock::now();
    pushLatencies.push_back(
        std::chrono::duration<double, std::nano>(eventEnd - eventStart)
            .count());
  }
  auto pushEnd = std::chrono::steady_clock::now();

  // --- Batch timing for pops ---
  std::vector<double> popLatencies;
  popLatencies.reserve(numEvents);

  auto popStart = std::chrono::steady_clock::now();
  for (size_t i = 0; i < numEvents; ++i) {
    auto eventStart = std::chrono::steady_clock::now();

    AgentEvent e;
    agentEventQueue.pop();

    auto eventEnd = std::chrono::steady_clock::now();
    popLatencies.push_back(
        std::chrono::duration<double, std::nano>(eventEnd - eventStart)
            .count());
  }
  auto popEnd = std::chrono::steady_clock::now();

  // --- Helper lambda for latency stats ---
  auto computeStats = [](std::vector<double> &latencies) {
    std::sort(latencies.begin(), latencies.end());
    size_t n = latencies.size();
    double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0) / n;

    auto p = [&](double percentile) {
      return latencies[std::min(n - 1, size_t(n * percentile))];
    };

    return std::make_tuple(avg, p(0.50), p(0.95), p(0.99), p(0.999), p(0.9999),
                           latencies.back());
  };

  auto [avgPush, p50Push, p95Push, p99Push, p999Push, p9999Push, maxPush] =
      computeStats(pushLatencies);
  auto [avgPop, p50Pop, p95Pop, p99Pop, p999Pop, p9999Pop, maxPop] =
      computeStats(popLatencies);

  // --- Report ---
  double pushDurationMs =
      std::chrono::duration<double, std::milli>(pushEnd - pushStart).count();
  double popDurationMs =
      std::chrono::duration<double, std::milli>(popEnd - popStart).count();

  std::cout << "+---------------- Push Stats ----------------+" << std::endl;
  std::cout << "Total events: " << numEvents << "\n"
            << "Duration: " << pushDurationMs << " ms\n"
            << "Throughput: " << numEvents / (pushDurationMs / 1000.0)
            << " ops/sec\n"
            << "Avg latency: " << avgPush << " ns, p50: " << p50Push
            << " ns, p95: " << p95Push << " ns, max: " << maxPush << " ns\n";

  std::cout << "+---------------- Pop Stats ----------------+" << std::endl;
  std::cout << "Duration: " << popDurationMs << " ms\n"
            << "Throughput: " << numEvents / (popDurationMs / 1000.0)
            << " ops/sec\n"
            << "Avg latency: " << avgPop << " ns, p50: " << p50Pop
            << " ns, p95: " << p95Pop << " ns, max: " << maxPop << " ns\n";
}
