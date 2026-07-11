#include "TradeDispatcher.h"
#include "Agent.h"
#include "Trade.h"

void TradeDispatcher::Attach(Agent *agent) {
  const ClientRef ref = agent->GetClientRef();
  if (clients_.size() <= ref) {
    clients_.resize(ref + 1, nullptr);
  }
  clients_[ref] = agent;
}

void TradeDispatcher::Detach(Agent *agent) {
  const ClientRef ref = agent->GetClientRef();
  if (ref < clients_.size() && clients_[ref] == agent) {
    clients_[ref] = nullptr;
  }
}

void TradeDispatcher::PushTradeInfo(Trade &&trade) {
  TradeInfo askTrade = trade.GetAskTrade();
  TradeInfo bidTrade = trade.GetBidTrade();
  if (askTrade.orderType != OrderType::CANCEL) {
    if (Agent *client = ClientFor(askTrade.clientRef)) {
      client->PushTrade(std::move(askTrade));
    }
  }
  if (bidTrade.orderType != OrderType::CANCEL) {
    if (Agent *client = ClientFor(bidTrade.clientRef)) {
      client->PushTrade(std::move(bidTrade));
    }
  }
}

void TradeDispatcher::PushTradeInfo(TradeInfo &&tradeInfo) {
  if (Agent *client = ClientFor(tradeInfo.clientRef)) {
    client->PushTrade(std::move(tradeInfo));
  }
}
