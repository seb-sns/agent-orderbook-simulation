#pragma once

#include "Order.h"
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <vector>

using PoolIndex = std::int64_t;
static constexpr int MAX_ORDERS = 1'048'576;
static constexpr int64_t INVALID_POOL_INDEX = -1;

// Lock-free free-list (Treiber stack). The head packs a 32-bit generation
// tag with the 32-bit slot index; the tag increments on every successful
// pop/push so a stale head from before an intervening pop/re-push cannot CAS
// successfully (the classic ABA problem for lock-free stacks).
class OrderPool {

private:
  struct Node {
    Order order;
    std::atomic<std::int64_t> next_free{INVALID_POOL_INDEX};
  };
  static constexpr std::uint32_t INVALID_SLOT = 0xFFFFFFFFu;

  static constexpr std::uint64_t Pack(std::uint64_t tag, std::uint32_t slot) {
    return (tag << 32) | slot;
  }
  static constexpr std::uint32_t SlotOf(std::uint64_t head) {
    return static_cast<std::uint32_t>(head);
  }
  static constexpr std::uint64_t TagOf(std::uint64_t head) {
    return head >> 32;
  }
  static constexpr std::uint32_t EncodeIndex(std::int64_t index) {
    return index == INVALID_POOL_INDEX ? INVALID_SLOT
                                       : static_cast<std::uint32_t>(index);
  }

  std::vector<Node> orders_;
  std::atomic<std::uint64_t> free_head_{Pack(0, 0)};

public:
  OrderPool() : orders_(MAX_ORDERS) {
    static_assert(MAX_ORDERS < INVALID_SLOT, "slot indexes must fit 32 bits");
    for (std::size_t i = 0; i < MAX_ORDERS - 1; ++i) {
      orders_[i].next_free.store(i + 1, std::memory_order_relaxed);
    }
    orders_[MAX_ORDERS - 1].next_free.store(INVALID_POOL_INDEX,
                                            std::memory_order_relaxed);
  };

  Order *get_order(PoolIndex index) { return &orders_[index].order; }

  PoolIndex allocate() {
    std::uint64_t head = free_head_.load(std::memory_order_acquire);
    while (true) {
      const std::uint32_t slot = SlotOf(head);
      if (slot == INVALID_SLOT) {
        throw std::logic_error("Allocating from a full OrderPool");
      }
      const std::int64_t next =
          orders_[slot].next_free.load(std::memory_order_relaxed);
      const std::uint64_t desired = Pack(TagOf(head) + 1, EncodeIndex(next));
      if (free_head_.compare_exchange_weak(head, desired,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
        return slot;
      }
    }
  };

  void deallocate(PoolIndex index) {
    std::uint64_t head = free_head_.load(std::memory_order_acquire);
    while (true) {
      const std::uint32_t slot = SlotOf(head);
      orders_[index].next_free.store(slot == INVALID_SLOT
                                         ? INVALID_POOL_INDEX
                                         : static_cast<std::int64_t>(slot),
                                     std::memory_order_relaxed);
      const std::uint64_t desired =
          Pack(TagOf(head) + 1, static_cast<std::uint32_t>(index));
      if (free_head_.compare_exchange_weak(head, desired,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
        return;
      }
    }
  };
};
