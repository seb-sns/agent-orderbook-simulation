#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

using OrderId = std::uint64_t;
using Price = double;
using Quantity = std::uint32_t;
using ClientRef = std::uint64_t;
using Timestamp = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };

enum class OrderType : std::uint8_t { LIMIT, MARKET, CANCEL };
class Order {
private:
  Price price_;
  Quantity remainingQuantity_;
  std::int64_t index_{-1};
  std::int64_t prev_{-1}; // Prev -> closer to the head (older orders)
  std::int64_t next_{-1}; // Next -> closer to the tail (newer orders)
  Quantity initialQuantity_;
  OrderType type_;
  Side side_;
  OrderId id_;
  ClientRef clientRef_;
  Timestamp timestamp_{0}; // agent-local sequence number, see Agent::TrackOrder

public:
  OrderId GetOrderId() const { return id_; }
  OrderType GetOrderType() const { return type_; }
  ClientRef GetClientRef() const { return clientRef_; }
  Side GetSide() const { return side_; }
  Price GetPrice() const { return price_; }
  Quantity GetInitialQuantity() const { return initialQuantity_; }
  Quantity GetRemainingQuantity() const { return remainingQuantity_; }
  std::int64_t GetIndex() const { return index_; }
  Timestamp GetTimestamp() const { return timestamp_; }
  std::int64_t GetPrev() const { return prev_; }
  std::int64_t GetNext() const { return next_; }

  void SetOrderId(const OrderId id) { id_ = id; }
  void SetOrderType(const OrderType type) { type_ = type; }
  void SetClientRef(const ClientRef clientRef) { clientRef_ = clientRef; }
  void SetSide(const Side side) { side_ = side; }
  void SetPrice(const Price price) { price_ = price; }
  void SetInitialQuantity(const Quantity quantity) {
    initialQuantity_ = quantity;
  }
  void SetRemainingQuantity(const Quantity quantity) {
    remainingQuantity_ = quantity;
  }
  void SetIndex(const std::int64_t index) { index_ = index; }
  void SetTimestamp(const Timestamp timestamp) { timestamp_ = timestamp; }
  void SetPrev(const std::int64_t index) { prev_ = index; }
  void SetNext(const std::int64_t index) { next_ = index; }

  Order(OrderId orderId, OrderType orderType, ClientRef clientRef, Side side,
        Price price, Quantity quantity)
      : id_(orderId), type_(orderType), clientRef_(clientRef), side_(side),
        price_(price), initialQuantity_(quantity),
        remainingQuantity_(quantity) {}
  Order() {}

  bool isFilled() const { return remainingQuantity_ == 0; }

  Quantity Fill(Order &order) {
    const Quantity orderRemaining = order.GetRemainingQuantity();
    const Quantity fillQuantity = std::min(remainingQuantity_, orderRemaining);
    remainingQuantity_ -= fillQuantity;
    order.SetRemainingQuantity(orderRemaining - fillQuantity);
    return fillQuantity;
  }
};

using Orders = std::vector<Order>;
