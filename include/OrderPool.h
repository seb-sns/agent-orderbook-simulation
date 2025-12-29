#pragma once

#include "Order.h"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <vector>

using PoolIndex = std::int64_t;
static constexpr int MAX_ORDERS = 50'000'000;
static constexpr int64_t INVALID_POOL_INDEX = -1;
class OrderPool {

private:
  struct Node {
    Order order;
    std::int64_t next_free;
  };
  std::vector<Node> orders_;
  std::int64_t free_head_{0};
  std::mutex mtx_;

public:
  OrderPool() {
    orders_.resize(MAX_ORDERS);
    for (std::size_t i = 0; i < MAX_ORDERS - 1; ++i) {
      orders_[i].next_free = i + 1;
    }
    orders_[MAX_ORDERS - 1].next_free = INVALID_POOL_INDEX;
  };

  Order *get_order(PoolIndex index) {
    std::lock_guard<std::mutex> lock(mtx_);
    return &orders_.at(index).order;
  }

  PoolIndex allocate() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (free_head_ == INVALID_POOL_INDEX) {
      throw std::logic_error("Allocating from a full OrderPool");
    }
    auto index = free_head_;
    free_head_ = orders_[index].next_free;
    return index;
  };

  void deallocate(PoolIndex index) {
    std::lock_guard<std::mutex> lock(mtx_);
    orders_[index].next_free = free_head_;
    free_head_ = index;
  };
};
