#pragma once

// Event recorder for the replay visualizer (viz/viewer.html).
//
// This header is safe to include from any build. Without ENABLE_VIZ the
// VIZ_EMIT_* macros expand to nothing, so the hot path of the normal
// 'simulation' target is byte-identical to a build without this file.
// The 'simulation_viz' target defines ENABLE_VIZ and pays one SPSC
// ring-buffer push (~a few ns) per book mutation; a dedicated writer
// thread drains events to a binary log. Pushes never block: if the
// writer falls behind, events are dropped and counted.

#ifndef ENABLE_VIZ

#define VIZ_EMIT_ADD(...) ((void)0)
#define VIZ_EMIT_TRADE(...) ((void)0)
#define VIZ_EMIT_CANCEL(...) ((void)0)

#else

#include "RingBuffer.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

enum class VizEventType : std::uint8_t { ADD = 0, TRADE = 1, CANCEL = 2 };

// One book mutation. 32 bytes, little-endian on disk.
// ADD:    order rested on the book. quantity = resting quantity.
// TRADE:  fill. side = aggressor side, clientRef = aggressor,
//         counterparty = resting agent, orderId = resting order,
//         quantity = fill quantity, priceIndex = trade price level.
// CANCEL: resting order removed. quantity = remaining quantity.
struct VizEvent {
  std::uint64_t tsNanos;
  std::uint64_t orderId;
  std::uint32_t clientRef;
  std::uint32_t quantity;
  std::uint32_t counterparty;
  std::uint16_t priceIndex;
  std::uint8_t type;
  std::uint8_t side; // 0 = buy, 1 = sell
};
static_assert(sizeof(VizEvent) == 32, "VizEvent must stay 32 bytes");

struct VizAgentMeta {
  std::uint32_t clientRef;
  std::uint8_t strategy; // 0=Random 1=MarketMaker 2=MomentumTrader 3=MeanReverter 4=Whale
  std::uint8_t pad[3]{};
};
static_assert(sizeof(VizAgentMeta) == 8, "VizAgentMeta must stay 8 bytes");

class VizRecorder {
public:
  static VizRecorder &Get() {
    static VizRecorder recorder;
    return recorder;
  }

  // All setup runs before the matching engine thread starts.
  void AddAgent(std::uint32_t clientRef, std::uint8_t strategy) {
    agents_.push_back({clientRef, strategy});
  }
  bool Start(const std::string &path, std::int64_t initialCash,
             std::int64_t initialUnits, double minPrice, double tickSize);
  void Stop();

  std::uint64_t GetDropped() const { return dropped_; }
  std::uint64_t GetRecorded() const { return recorded_; }

  // Hot path: called only from the matching engine thread (single producer).
  void Emit(VizEventType type, std::uint64_t orderId, std::uint32_t clientRef,
            std::uint32_t counterparty, std::uint16_t priceIndex,
            std::uint32_t quantity, std::uint8_t side) {
    VizEvent event{
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0_)
                .count()),
        orderId,
        clientRef,
        quantity,
        counterparty,
        priceIndex,
        static_cast<std::uint8_t>(type),
        side};
    if (!events_.Push(event)) {
      ++dropped_;
    }
  }

private:
  VizRecorder() = default;
  void WriterLoop();

  RingBuffer<VizEvent, 1 << 16> events_;
  std::vector<VizAgentMeta> agents_;
  std::thread writer_;
  std::FILE *file_{nullptr};
  std::chrono::steady_clock::time_point t0_;
  std::uint64_t dropped_{0};
  std::uint64_t recorded_{0};
  std::atomic<bool> running_{false};
};

#define VIZ_EMIT_ADD(orderId, clientRef, priceIndex, quantity, side)          \
  VizRecorder::Get().Emit(VizEventType::ADD, (orderId),                       \
                          static_cast<std::uint32_t>(clientRef), 0,           \
                          static_cast<std::uint16_t>(priceIndex),             \
                          (quantity),                                         \
                          (side) == Side::Buy ? 0 : 1)

#define VIZ_EMIT_TRADE(orderId, aggressorRef, restingRef, priceIndex,         \
                       quantity, aggressorSide)                               \
  VizRecorder::Get().Emit(VizEventType::TRADE, (orderId),                     \
                          static_cast<std::uint32_t>(aggressorRef),           \
                          static_cast<std::uint32_t>(restingRef),             \
                          static_cast<std::uint16_t>(priceIndex),             \
                          (quantity),                                         \
                          (aggressorSide) == Side::Buy ? 0 : 1)

#define VIZ_EMIT_CANCEL(orderId, clientRef, priceIndex, quantity, side)       \
  VizRecorder::Get().Emit(VizEventType::CANCEL, (orderId),                    \
                          static_cast<std::uint32_t>(clientRef), 0,           \
                          static_cast<std::uint16_t>(priceIndex),             \
                          (quantity),                                         \
                          (side) == Side::Buy ? 0 : 1)

#endif // ENABLE_VIZ
