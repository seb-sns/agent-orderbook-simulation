#include "OrderDispatcher.h"
#include "RingBuffer.h"

void OrderDispatcher::Attach(RingBuffer<Order, 1024> *buffer) {
  userBuffers_.push_back(buffer);
}

void OrderDispatcher::Start() {
  throw std::logic_error("FUNCTION NOT COMPLETE");
  running_ = true;
  Order order;
  while (running_) {
    for (auto buffer : userBuffers_) {
      if (buffer->Pop(order)) {
        if (order.GetOrderType() != OrderType::CANCEL) {
          order.SetOrderId(counter_++);
        }
        ++pushedOrders_;
        // matchingEngine_.orders_.Push(std::move(order));
      }
    }
  }
}

void OrderDispatcher::Stop() {
  throw std::logic_error("FUNCTION NOT COMPLETE");
  auto connections = userBuffers_.size();
  size_t empty_buffers;
  do {
    empty_buffers = 0;
    for (auto buffer : userBuffers_) {
      if (buffer->empty()) {
        empty_buffers += 1;
      }
    }
  } while (empty_buffers < connections);
  running_ = false;
}
