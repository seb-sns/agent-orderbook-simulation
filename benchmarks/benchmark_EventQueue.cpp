#include "Agent.h"
#include "EventQueue.h"
#include "ThreadPin.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <queue>
#include <random>
#include <vector>

// ---------- candidate structures (hold-model API: top / replace_top / push) --

struct ShippedEventQueue {
  EventQueue<AgentEvent> q;
  void push(const AgentEvent &e) { q.Push(e); }
  AgentEvent top() const { return q.Top(); }
  void replace_top(const AgentEvent &e) { q.ReplaceTop(e); }
};

struct StdPQ {
  std::priority_queue<AgentEvent> q;
  void push(const AgentEvent &e) { q.push(e); }
  AgentEvent top() const { return q.top(); }
  void replace_top(const AgentEvent &e) { q.pop(); q.push(e); }
};

template <int ARITY> struct DHeap {
  std::vector<AgentEvent> h;
  void push(const AgentEvent &e) {
    h.push_back(e);
    std::size_t i = h.size() - 1;
    while (i && h[(i - 1) / ARITY].time > h[i].time) {
      std::swap(h[(i - 1) / ARITY], h[i]);
      i = (i - 1) / ARITY;
    }
  }
  AgentEvent top() const { return h[0]; }
  void replace_top(const AgentEvent &e) {
    h[0] = e;
    std::size_t i = 0;
    while (true) {
      std::size_t best = i;
      const std::size_t first = i * ARITY + 1;
      const std::size_t last = std::min(first + ARITY, h.size());
      for (std::size_t c = first; c < last; ++c)
        if (h[c].time < h[best].time) best = c;
      if (best == i) return;
      std::swap(h[i], h[best]);
      i = best;
    }
  }
};

// One slot per agent: pop is a linear argmin scan, update is O(1).
struct FlatArgmin {
  std::vector<double> t;
  void push(const AgentEvent &e) {
    if (t.size() <= e.pos) t.resize(e.pos + 1, 1e300);
    t[e.pos] = e.time;
  }
  AgentEvent top() const {
    std::size_t best = 0;
    double bt = t[0];
    for (std::size_t i = 1; i < t.size(); ++i)
      if (t[i] < bt) { bt = t[i]; best = i; }
    return {bt, best};
  }
  void replace_top(const AgentEvent &e) { t[e.pos] = e.time; }
};

// Correct, simple calendar queue: N buckets of width W cover one "year";
// events beyond the year go to an overflow list, re-seeded on wrap.
struct Calendar {
  static constexpr std::size_t N = 256;
  double width, start{0};
  std::size_t cursor{0};
  std::vector<std::vector<AgentEvent>> buckets{N};
  std::vector<AgentEvent> overflow;
  explicit Calendar(double w = 0.5) : width(w) {}
  void push(const AgentEvent &e) {
    const double rel = e.time - start;
    if (rel >= N * width) { overflow.push_back(e); return; }
    std::size_t idx = rel <= 0 ? cursor : static_cast<std::size_t>(rel / width);
    if (idx < cursor) idx = cursor; // never behind the cursor
    buckets[idx].push_back(e);
  }
  AgentEvent top() {
    while (true) {
      for (; cursor < N; ++cursor)
        if (!buckets[cursor].empty()) {
          auto &b = buckets[cursor];
          std::size_t best = 0;
          for (std::size_t i = 1; i < b.size(); ++i)
            if (b[i].time < b[best].time) best = i;
          if (best != 0) std::swap(b[0], b[best]); // min at front
          return b[0];
        }
      // year exhausted: advance the epoch and re-seed from overflow
      start += N * width;
      cursor = 0;
      std::vector<AgentEvent> pending;
      pending.swap(overflow);
      for (const AgentEvent &e : pending) push(e);
    }
  }
  void replace_top(const AgentEvent &e) {
    auto &b = buckets[cursor];
    b[0] = b.back();
    b.pop_back();
    push(e);
  }
};

// ---------- harness ---------------------------------------------------------

struct Workload {
  const char *name;
  std::vector<double> meanGap; // per agent
};

template <typename Q>
double run(Q &&q, const Workload &w, long ops, bool checkOrder) {
  const std::size_t n = w.meanGap.size();
  // precompute gaps so RNG cost stays out of the measurement
  std::mt19937 rng(7);
  std::vector<std::vector<double>> gaps(n);
  std::vector<std::size_t> gi(n, 0);
  for (std::size_t a = 0; a < n; ++a) {
    std::exponential_distribution<double> d(1.0 / w.meanGap[a]);
    gaps[a].resize(4096);
    for (auto &g : gaps[a]) g = d(rng);
    q.push({gaps[a][0], a});
  }
  std::vector<long> popsPer(n, 0);
  double prev = -1;
  long misordered = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (long i = 0; i < ops; ++i) {
    AgentEvent e = q.top();
    if (checkOrder) {
      if (e.time < prev) ++misordered;
      prev = e.time;
      popsPer[e.pos]++;
    }
    std::size_t &k = gi[e.pos];
    k = (k + 1) & 4095;
    q.replace_top({e.time + gaps[e.pos][k], e.pos});
  }
  auto t1 = std::chrono::steady_clock::now();
  if (checkOrder) {
    // starvation check: every agent's pop share vs its rate share
    double rateSum = 0;
    for (double m : w.meanGap) rateSum += 1.0 / m;
    long starved = 0;
    for (std::size_t a = 0; a < n; ++a) {
      double expect = ops * (1.0 / w.meanGap[a]) / rateSum;
      // Only meaningful when the expected count is large enough that a 2x
      // shortfall can't be Poisson noise.
      if (expect >= 100 && popsPer[a] < expect * 0.5) ++starved;
    }
    if (misordered || starved)
      std::printf("    !! CORRECTNESS: %ld misordered pops, %ld starved agents\n",
                  misordered, starved);
  }
  return std::chrono::duration<double, std::nano>(t1 - t0).count() / ops;
}

Workload makeMix(int scale) {
  // mirrors the sim default ratios: 40 random@2 + 10 mm@2 + 5 mom@2 : 5 rev@3 : 2 whale@40
  Workload w{"sim-mix", {}};
  for (int i = 0; i < 55 * scale; ++i) w.meanGap.push_back(2.0);
  for (int i = 0; i < 5 * scale; ++i) w.meanGap.push_back(3.0);
  for (int i = 0; i < 2 * scale; ++i) w.meanGap.push_back(40.0);
  return w;
}

int main() {
  PinCurrentThreadToCore(2);
  constexpr long OPS = 3'000'000;
  constexpr int ROUNDS = 3; // interleaved; min-of-rounds cancels clock ramp-up
  for (int scale : {1, 4, 12, 66}) { // 62, 248, 744, 4092 agents
    Workload w = makeMix(scale);
    std::printf("── %zu agents (sim mix incl. rare whales) ──\n",
                w.meanGap.size());
    const char *names[] = {"std::priority_queue ", "binary heap (replace)",
                           "EventQueue (shipped) ", "4-ary heap (replace) ",
                           "flat argmin array    ", "calendar queue       "};
    double best[6];
    for (double &b : best) b = 1e300;
    for (int round = 0; round < ROUNDS; ++round) {
      const bool check = round == 0; // correctness checked once per scale
      best[0] = std::min(best[0], run(StdPQ{}, w, OPS, check));
      best[1] = std::min(best[1], run(DHeap<2>{}, w, OPS, check));
      best[2] = std::min(best[2], run(ShippedEventQueue{}, w, OPS, check));
      best[3] = std::min(best[3], run(DHeap<4>{}, w, OPS, check));
      best[4] = std::min(best[4], run(FlatArgmin{}, w, OPS, check));
      best[5] = std::min(best[5], run(Calendar{0.5}, w, OPS, check));
    }
    for (int i = 0; i < 6; ++i) {
      std::printf("  %s: %6.1f ns/op\n", names[i], best[i]);
    }
  }
  return 0;
}
