#pragma once
#include "Agent.h"
#include "AgentStrategy.h"
#include "LadderQueue.h"
#include <atomic>
#include <memory>
#include <vector>

constexpr inline auto accessor = [](const AgentEvent &event) {
  return event.time;
};

class AgentManager {
public:
  AgentManager(std::uint64_t maxTime);

  void SetRunning(bool running);

  void AddAgent(std::unique_ptr<Agent> agent);
  void PushAgentEvent(AgentEvent &&event);
  void WarmUp();
  void RunOutgoingLoop();
  void RunIncomingLoop();

  std::uint64_t GetNAgentActions() const;

  void PrintStates();
  void PrintSummary();

  std::atomic<bool> running_{false};
  std::uint64_t currentTime_{0};
  std::uint64_t maxTime_;
  std::uint64_t agentActions_{0};
  std::vector<std::unique_ptr<Agent>> agents_;
  LadderQueue agentEventQueue_;
};
