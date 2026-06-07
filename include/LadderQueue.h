#pragma once
#include "Agent.h"
#include <cstddef>
#include <queue>
#include <vector>

using Bucket = std::vector<AgentEvent>;
struct Rung {
  double start_time;
  double bucket_width;
  std::vector<Bucket> buckets;
  std::size_t curr{0};

  Rung(int numBuckets, double start, double width)
      : start_time(start), bucket_width(width), buckets(numBuckets) {};

  void push(const AgentEvent &event) {
    int idx = static_cast<int>((event.time - start_time) / bucket_width);
    if (idx < 0) {
      idx = 0;
    }
    if (idx >= buckets.size()) {
      idx = buckets.size() - 1;
    }
    buckets[idx].push_back(event);
  }

  bool empty() const {
    for (const auto &bucket : buckets) {
      if (!bucket.empty()) {
        return false;
      }
    }

    return true;
  }

  double get_current_bucket_start() const {
    return start_time + (curr * bucket_width);
  }
};
constexpr std::size_t N_BUCKETS{1024};

class LadderQueue {
private:
  std::priority_queue<AgentEvent> topRung;
  std::vector<Rung> lowerRungs;

  double currentTime{0};
  double topRungWidth{5};

public:
  LadderQueue() {
    lowerRungs.emplace_back(N_BUCKETS, 0.0, 5.0);
    lowerRungs.emplace_back(N_BUCKETS, 0.0, 50.0);
    lowerRungs.emplace_back(N_BUCKETS, 0.0, 500.0);
  }

  void Push(const AgentEvent &event) {
      if (event.time - currentTime <= topRungWidth) {
          topRung.push(event);
          return;
      }

      // Determine which lower rung
      double dt = event.time - currentTime;
      Rung* targetRung = nullptr;

      if (dt <= lowerRungs[0].bucket_width * N_BUCKETS) {
          targetRung = &lowerRungs[0];
      } else if (dt <= lowerRungs[1].bucket_width * N_BUCKETS) {
          targetRung = &lowerRungs[1];
      } else {
          targetRung = &lowerRungs[2];
      }

      // Compute bucket index based on rung start
      int idx = static_cast<int>((event.time - targetRung->start_time) / targetRung->bucket_width);

      if (idx < 0) {
          topRung.push(event);
      } 
      else if (idx >= N_BUCKETS) {
          topRung.push(event); 
      } 
      else if (static_cast<std::size_t>(idx) < targetRung->curr) {
          // 1. Check strict index comparison (handles past events)
          topRung.push(event);
      }
      else if (event.time < targetRung->get_current_bucket_start()) {
          // 2. Check for the "Cursor Drift / Rounding" bug.
          // If the event time is *before* the start time of the current bucket,
          // it means it falls into the previous bucket which we already processed.
          // This catches the case where idx == curr, but rounding made it land in the "past" of that bucket.
          topRung.push(event);
      }
      else {
          targetRung->buckets[idx].push_back(event);
      }
  }

  bool Pop(AgentEvent &event) {
      // 1. If topRung has events, process them
      if (!topRung.empty()) {
          event = topRung.top();
          topRung.pop();
          currentTime = event.time;
          return true;
      }

      // 2. If topRung is empty, we MUST scan Lower Rungs to find the next event.
      // We cannot just rely on 'rung.curr' because it might be pointing to an empty bucket
      // while the next bucket is full of events that are due sooner than anything in Top Rung.

      for (auto &rung : lowerRungs) {
          // Optimization: Keep track of the index we want to transfer
          std::size_t target_idx = N_BUCKETS; 

          // Scan from current cursor to find the next non-empty bucket
          for (std::size_t i = rung.curr; i < N_BUCKETS; ++i) {
              if (!rung.buckets[i].empty()) {
                  target_idx = i;
                  break;
              }
          }

          // If we found a bucket in the current sweep
          if (target_idx != N_BUCKETS) {
              // Move this bucket to topRung
              auto &bucket = rung.buckets[target_idx];
              for (auto &e : bucket) {
                  topRung.push(e);
              }
              bucket.clear();

              // Advance cursor past this bucket
              rung.curr = target_idx + 1;

              // Now that topRung is populated, pop the actual event
              event = topRung.top();
              topRung.pop();
              currentTime = event.time;

              // Handle Wrap-Around logic
              if (rung.curr >= N_BUCKETS) {
                  rung.curr = 0;
                  rung.start_time = currentTime;
              }
              
              return true;
          }

          // If we scanned the rest of the rung and found nothing, 
          // reset for the next cycle (standard Calendar Queue wrap-around)
          rung.curr = 0;
          rung.start_time = currentTime;
      }

      return false;
  }
};
