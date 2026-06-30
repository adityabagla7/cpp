#pragma once

#include <cstdint>

namespace ob {

// Prices are stored as integers (ticks / fixed-point), never as floating point.
// Real exchange feeds quote in integer ticks; using int64 keeps comparisons
// exact and branch-predictable, and avoids float rounding in the hot path.
using Price    = std::int64_t;
using Quantity = std::uint64_t;
using OrderId  = std::uint64_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

// A single fill produced when an aggressing (taker) order crosses a resting
// (maker) order. The engine emits one Trade per maker consumed.
struct Trade {
    OrderId  taker_id;
    OrderId  maker_id;
    Price    price;     // executes at the resting maker's price (price priority)
    Quantity quantity;
};

} // namespace ob
