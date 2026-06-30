#pragma once

#include "types.hpp"

namespace ob {

// An Order is a node in an intrusive doubly-linked list. Each price level owns
// a FIFO list of these nodes (time priority). Intrusive linkage lets us cancel
// an order in O(1) given only its address -- no container lookup, no shifting.
struct Order {
    OrderId  id{};
    Price    price{};
    Quantity quantity{};
    Side     side{};

    Order* prev{nullptr};
    Order* next{nullptr};
};

} // namespace ob
