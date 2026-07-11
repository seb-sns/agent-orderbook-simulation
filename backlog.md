# Backlog

## Latency assessment (perf profile of a full sim run, 2026-07-05)

Where cycles go across the three threads, after all current optimizations:

- ~32% incoming thread polling agent ring buffers (mostly empty) — now
  mitigated with exponential backoff; the residual is the price of busy-wait
  polling. A further step would be event notification (futex/eventfd) at the
  cost of wake latency.
- ~25% matching engine (Start/match/book) — already ~76ns median per order.
  TradeInfo has since been packed 80 → 40 bytes (one cache line; dropped the
  unread poolIndex, narrowed refs/seq to u32, u8 enums) — measured +4–21%
  order throughput across interleaved rounds. The remaining idea is price
  quantisation to u16 grid indexes (32 bytes) — rejected for now because
  agents reconstruct cash reservations from orderPrice and off-grid prices
  would silently corrupt accounting. The ACK message costs one dispatcher
  push per resting order (the price of the lock-free cancel protocol).
- ~16% outgoing thread (strategies + bookkeeping). unordered_map operations
  for active-order records were the visible cost (now reserved to avoid
  rehash); replacing it with an open-addressing map is the next step if it
  ever matters.
- Per-action OrderPtrs vector churn measured under 2% of cycles — not worth
  restructuring.

## Ideas

- Position cap / max drawdown for Mean Reverter agents so they stop averaging
  into a level the market has permanently repriced away from fair value.
- Price-grid boundary quirk: orders priced outside [100, 120] are clamped to a
  grid index but keep their raw price, so a trade at the boundary can print at
  a price slightly off its level's price.
- Backpressure hardening: `Agent::PushTrade` (engine → agent ring) still spins
  if an agent's incoming buffer stays full. The incoming thread itself never
  blocks (order-event overflow spills to a vector), so this can only stall if
  the incoming thread is starved outright; a spill-to-heap fallback like the
  order-event one would make the engine fully non-blocking too.

## Done (kept for context)

- ~~LadderQueue misordering~~ — the custom LadderQueue delivered events badly
  out of time order (147k of 2M pops in a reproduction harness, with slow
  agents starved 12:1 and their backlog replayed in a burst at the end of the
  run — this emptied the book at shutdown and corrupted whale P&L; it also
  silently dropped ~75% of scheduled agent activity per run). Replaced with
  `include/EventQueue.h`: a binary min-heap with a ReplaceTop operation
  (the loop always pops then pushes the same agent's next event, so one
  sift-down does both). Chosen by measurement over std::priority_queue,
  4-ary heap, flat argmin and a correct calendar queue — see
  `benchmarks/benchmark_eventqueue` (interleaved min-of-3 rounds): at
  62/248/744 agents the shipped queue is best or near-best (27.6/43.6/53.0
  ns per op vs 32.6/44.9/56.3 for std::priority_queue); the calendar queue
  edges it at 62 agents but collapses at scale (352 ns at 4092) and is the
  structure class that's hardest to keep correct.
  `include/LadderQueue.h` is kept for reference/repair — if it's fixed,
  Pop must compare topRung's front against the earliest rung bucket, and
  rung resets must not re-bin buckets that still hold events.

- ~~Order-pool slot reuse race in agent cancel logic~~ — fixed by redesign:
  agents keep by-value `ActiveOrder` records keyed by an agent-local sequence
  number, the engine ACKs resting orders with their assigned id, and the
  incoming thread forwards ACK/retire events over an SPSC ring. No pool
  pointers cross threads anymore; `shared_mutex` removed (verified with
  ThreadSanitizer).
- ~~TradeDispatcher hash lookup per fill~~ — now a dense `vector<Agent*>`.
- ~~RunIncomingLoop popping one trade per agent per pass~~ — drains fully.
- ~~Spin loops without `_mm_pause()`~~ — pauses added in the engine poll/stop
  and agent push paths.
- ~~Re-baseline benchmarks~~ — see `rebaselined_simulation.txt` /
  `rebaselined_agentlatency.txt`; agent benchmark now also reports orders/s
  and its duplicate clientRef bug is fixed.
- ~~CPU frequency scaling / no thread pinning~~ — benchmark threads are now
  pinned to distinct physical cores (`include/ThreadPin.h`), and
  `benchmarks/run_benchmarks.sh` sets the performance governor for the run
  and restores the previous settings on exit.
