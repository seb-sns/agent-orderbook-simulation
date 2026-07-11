#pragma once

// Linux-only helpers to pin threads to cores for stable benchmarking.
// On this project's reference machine (4 physical cores + HT), pin the
// outgoing/engine/incoming threads to cpus 1/2/3 — distinct physical cores,
// leaving cpu0 (which services most interrupts) and the HT siblings free.

#include <cstdio>
#include <pthread.h>
#include <sched.h>
#include <thread>

inline bool PinThreadToCore(std::thread &thread, int core) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(core, &set);
  if (pthread_setaffinity_np(thread.native_handle(), sizeof(set), &set) != 0) {
    std::fprintf(stderr, "warning: failed to pin thread to core %d\n", core);
    return false;
  }
  return true;
}

inline bool PinCurrentThreadToCore(int core) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(core, &set);
  if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0) {
    std::fprintf(stderr, "warning: failed to pin thread to core %d\n", core);
    return false;
  }
  return true;
}
