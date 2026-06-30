# Low-Latency Limit Order Book & Matching Engine

A price-time-priority limit order book with an embedded matching engine, written
in modern C++ (C++20) with an emphasis on **allocation-free hot paths**,
**O(1) cancels**, and **measured latency**. Header-only core, dependency-free.

This is the data structure that sits at the heart of every exchange and every
trading system: orders arrive, cross resting liquidity at the best available
price, generate fills, and rest any remainder. The engine here processes ~10M
orders/sec on a single core with a median `submit()` latency in the low
hundreds of nanoseconds.

---

## Build & run

```bash
make test     # build and run the correctness suite
make bench    # build and run the latency/throughput benchmark
make demo     # build and run a small end-to-end demo
```

Requires a C++20 compiler. No external dependencies. A `CMakeLists.txt` is also
provided for CMake users.

---

## What it does

- **Limit orders** match against the opposite side, best price first, FIFO
  within a price level, then rest any unfilled remainder.
- **Cancels** remove a resting order in O(1) given its id.
- **Top-of-book** and per-level depth queries in O(1) / O(log n).
- Every fill is reported through a caller-supplied trade callback.

```cpp
ob::OrderBook book;
book.submit(1, ob::Side::Sell, 10050, 30);          // rests an ask
book.submit(2, ob::Side::Buy,  10100, 70,           // crosses it
            [](const ob::Trade& t){ /* handle fill */ });
```

---

## Architecture

```
                 submit(id, side, price, qty, on_trade)
                                  |
                         match against opposite side
                        (best price first, FIFO in level)
                                  |
                    +-------------+-------------+
                    |                           |
              emit Trade per fill         rest remainder
              (inlined callback)          in own-side book
                    |                           |
                    v                           v
              ObjectPool<Order> <----- intrusive FIFO price levels
              (no malloc in hot path)  (O(1) cancel by address)
```

| Component        | Structure                                   | Why |
|------------------|---------------------------------------------|-----|
| Price levels     | `std::map` keyed so `begin()` = best price  | sorted, correct, clean baseline |
| Orders in level  | intrusive doubly-linked FIFO                 | time priority; O(1) cancel/unlink |
| Order storage    | free-list `ObjectPool` over a `std::deque`  | no allocator on the hot path; stable addresses |
| id -> order      | `std::unordered_map<OrderId, Order*>`        | O(1) cancel lookup |
| Trade reporting  | template callback (not `std::function`)     | inlined; no type-erased indirect call |

---

## Design decisions (and their tradeoffs)

These are the choices an interviewer will probe, so they are made deliberately.

**No allocation on the hot path.** `malloc`/`free` have unbounded, cache-hostile
latency. Orders are recycled through an `ObjectPool`. The pool's backing store is
a `std::deque` rather than a `std::vector` specifically because the deque keeps
element addresses stable as it grows — a `vector` reallocation would invalidate
every outstanding `Order*`.

**Intrusive lists for O(1) cancel.** Each order *is* a list node. Cancelling
needs only the order's address (from the id index), then a constant-time unlink —
no search, no element shifting.

**Template callback, not `std::function`.** The trade listener is a template
parameter of `submit()`, so the compiler inlines it into the match loop. A
`std::function` would force a type-erased indirect call on the single hottest
path in the engine — exactly where you cannot afford it.

**`if constexpr` on order side.** Buy and sell matching are selected at compile
time, so the inner loop has no per-order branch on side.

**Why `std::map` for price levels — and when you'd replace it.** A red-black tree
gives correct, sorted access with `begin()` as the best price, which is clean and
plenty fast for this baseline. But it is pointer-chasing and cache-unfriendly. In
production, instruments trade in a bounded band around the mid, so the standard
upgrade is a **flat array indexed by price tick** with cached best-bid/ask
cursors: O(1) access, contiguous memory, no node allocation. That swap is the
single highest-impact optimization here and is the first item on the roadmap. The
matching logic is deliberately isolated from the level container so it can be
dropped in without touching the engine.

---

## Benchmark

1,000,000 randomly generated limit orders clustered in a ±100-tick band around a
10000 mid (so order flow continuously crosses and produces fills, not just
inserts). Inputs are pre-generated; the timed region contains only engine work.

```
== throughput ==
orders submitted : 1,000,000
trades generated : 770,035
total time       : 94.07 ms
throughput       : 10.63 M orders/sec
mean latency     : 94.1 ns/order

== submit() latency (per order) ==
p50              : 110 ns
p90              : 193 ns
p99              : 353 ns
p99.9            : 1190 ns
```

**Honest caveats** (methodology matters more than the raw numbers):
- Measured on a single shared/virtualized cloud vCPU (Xeon ~2.1 GHz), built with
  `-O3 -march=native`. On a pinned, isolated core your numbers will be lower and
  far more stable.
- Per-op latencies *include* the timer's own overhead (~29 ns of back-to-back
  `steady_clock` reads, measured and reported by the benchmark). On real hardware
  you would time with serialized `rdtscp` calibrated to the TSC frequency.
- The extreme tail (max) is dominated by OS scheduling jitter on a shared core,
  not by the algorithm — the classic reason production HFT systems pin threads,
  isolate cores, and disable frequency scaling.

---

## Correctness

`make test` runs a dependency-free suite covering: resting without crossing,
full and partial fills, price-time priority within a level, sweeping across
multiple price levels, cancels (including double-cancel), and the no-cross case
when the limit isn't met.

---

## Roadmap

These are deliberately scoped as follow-on phases:

1. **Flat-array price levels** indexed by tick + cached best cursors — the main
   latency win over the `std::map` baseline.
2. **Binary feed parser** (ITCH-style fixed-layout messages) to drive the book
   from a realistic market-data stream instead of synthetic input.
3. **Lock-free SPSC ring buffer** to decouple the feed-handler thread from the
   matching thread.
4. **Order modify / replace** semantics (price-improving vs. queue-position-
   losing) and self-trade prevention.

---

## Layout

```
include/orderbook/   header-only engine (types, order, object_pool, order_book)
tests/               correctness suite
bench/               latency + throughput benchmark
src/                 end-to-end demo
```
