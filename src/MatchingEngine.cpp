#include "MatchingEngine.h"
#include "Order.h"
#include <utility>

void MatchingEngine::Start() {
  running_ = true;
  Order *order = nullptr;
  while (running_) {
    if (orders_.Pop(order)) {
      order->SetOrderId(++counter_);
      ProcessOrder(std::move(order));
    }
  }
}

void MatchingEngine::Stop() {
  while (!orders_.empty()) {
  }
  running_ = false;
}

void MatchingEngine::ProcessOrder(Order *order) {
  ++ordersProcessed_;
  switch (order->GetOrderType()) {
  case OrderType::LIMIT:
    MatchLimitOrder(std::move(order));
    break;
  case OrderType::MARKET:
    MatchMarketOrder(std::move(order));
    break;
  case OrderType::CANCEL:
    CancelOrder(std::move(order));
    break;
  }
}

void MatchingEngine::MatchLimitOrder(Order *order) {
  if (order->GetSide() == Side::Buy) {
    while (order->GetRemainingQuantity() > 0) {
      auto index = orderbook_.GetBestAsk();
      if (!index) {
        orderbook_.AddOrder(std::move(order));
        return;
      }
      Price price = orderbook_.IndexToPrice(*index);
      if (price > order->GetPrice()) {
        orderbook_.AddOrder(std::move(order));
        return;
      }
      orderbook_.FillOrder(order, *index);
    }
    orderPool_->deallocate(order->GetIndex());
  } else {
    while (order->GetRemainingQuantity() > 0) {
      auto index = orderbook_.GetBestBid();
      if (!index) {
        orderbook_.AddOrder(std::move(order));
        return;
      }
      Price price = orderbook_.IndexToPrice(*index);
      if (price < order->GetPrice()) {
        orderbook_.AddOrder(std::move(order));
        return;
      }
      orderbook_.FillOrder(order, *index);
    }
    orderPool_->deallocate(order->GetIndex());
  }
}

//TO-DO
//Improve cancelling market orders that can't find any matching orders
void MatchingEngine::MatchMarketOrder(Order *order) {
  Side side = order->GetSide();
  while (order->GetRemainingQuantity() > 0) {
    auto index = (side == Side::Buy) ? orderbook_.GetBestAsk() : orderbook_.GetBestBid(); 
    if (!index) {
      Order* cancelOrder = CreateCancelOrder(order);
      orderbook_.AddOrder(std::move(order));
      CancelOrder(cancelOrder);
      return;
    }
    orderbook_.FillOrder(order, *index);
  }
  orderPool_->deallocate(order->GetIndex());
}

void MatchingEngine::CancelOrder(Order *order) {
  orderbook_.CancelOrder(order);
  orderPool_->deallocate(order->GetIndex());
}

Order* MatchingEngine::CreateCancelOrder(Order * order) {
  PoolIndex slot = orderPool_->allocate();
  Order *cancelOrder = orderPool_->get_order(slot);
  cancelOrder->SetOrderId(order->GetOrderId());
  cancelOrder->SetOrderType(OrderType::CANCEL);
  cancelOrder->GetClientRef(order->GetClientRef());
  cancelOrder->SetSide(order->GetSide());
  cancelOrder->SetPrice(order->GetPrice());
  cancelOrder->SetInitialQuantity(0);
  cancelOrder->SetRemainingQuantity(0);
  cancelOrder->SetIndex(slot);
  return cancelOrder;
};
