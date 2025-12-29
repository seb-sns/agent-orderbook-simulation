#pragma once
#include <cstdint>

struct PriceLevel {
  std::int64_t tail_{-1};
  std::int64_t head_{-1};
  bool empty() const { return head_ == -1; }
};
