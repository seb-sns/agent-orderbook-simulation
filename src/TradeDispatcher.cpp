#include "TradeDispatcher.h"
#include "Agent.h"
#include "Trade.h"

void TradeDispatcher::Attach(Agent *agent) {
  clients_[agent->GetClientRef()] = agent;
}

void TradeDispatcher::Detach(Agent *agent) {
  clients_.erase(agent->GetClientRef());
}

void TradeDispatcher::PushTradeInfo(Trade &&trade) {
  TradeInfo askTrade = trade.GetAskTrade();
  TradeInfo bidTrade = trade.GetBidTrade();
  if (askTrade.orderType != OrderType::CANCEL) {
    clients_[askTrade.clientRef]->PushTrade(std::move(askTrade));
  }
  if (bidTrade.orderType != OrderType::CANCEL) {
    clients_[bidTrade.clientRef]->PushTrade(std::move(bidTrade));
  }
}
