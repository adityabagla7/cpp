#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <unordered_map>

#include "object_pool.hpp"
#include "order.hpp"
#include "types.hpp"

namespace ob {

// A single price level: a FIFO list of orders enforcing time priority within
// a price, plus a cached aggregate quantity for O(1) depth queries.
struct PriceLevel {
    Order*   head{nullptr};
    Order*   tail{nullptr};
    Quantity total_qty{0};

    void push_back(Order* o) noexcept {
        o->next = nullptr;
        o->prev = tail;
        if (tail) tail->next = o;
        else      head = o;
        tail = o;
        total_qty += o->quantity;
    }

    void unlink(Order* o) noexcept {
        if (o->prev) o->prev->next = o->next;
        else         head = o->next;
        if (o->next) o->next->prev = o->prev;
        else         tail = o->prev;
        total_qty -= o->quantity;
    }

    [[nodiscard]] bool empty() const noexcept { return head == nullptr; }
};

// Price-time-priority limit order book with an embedded matching engine.
//
// Design choices that matter for latency (see README for the full discussion):
//   * Orders live in an ObjectPool -- zero heap traffic on the steady-state
//     hot path.
//   * Each price level is an intrusive FIFO list -> O(1) cancel by address.
//   * The trade callback is a *template parameter* of submit(), not a
//     std::function. That keeps the hot path free of type-erasure /
//     indirect-call overhead: the listener is inlined into the match loop.
//   * Price levels are kept in std::map keyed so that begin() is always the
//     best price. This is the clean, correct baseline; the README explains
//     when you would replace it with a flat array indexed by tick.
class OrderBook {
public:
    explicit OrderBook(std::size_t expected_orders = 1u << 16)
        : pool_(expected_orders) {
        index_.reserve(expected_orders);
    }

    // Convenience overload: submit and discard the resulting fills.
    void submit(OrderId id, Side side, Price price, Quantity qty) {
        submit(id, side, price, qty, [](const Trade&) noexcept {});
    }

    // Submit a limit order. It first matches against the opposite side
    // (best price first, FIFO within a price), emitting a Trade per fill via
    // on_trade, then rests any unfilled remainder in the book.
    //
    // OnTrade is any callable with signature void(const Trade&). Passing a
    // lambda lets the compiler inline it -- there is no std::function here.
    template <typename OnTrade>
    void submit(OrderId id, Side side, Price price, Quantity qty,
                OnTrade&& on_trade) {
        if (qty == 0) return;
        if (side == Side::Buy) {
            qty = match<Side::Buy>(id, price, qty, on_trade);
            if (qty) rest(id, Side::Buy, price, qty, bids_);
        } else {
            qty = match<Side::Sell>(id, price, qty, on_trade);
            if (qty) rest(id, Side::Sell, price, qty, asks_);
        }
    }

    // Cancel a resting order by id. O(1) average (hash lookup) + O(1) unlink.
    bool cancel(OrderId id) {
        auto it = index_.find(id);
        if (it == index_.end()) return false;
        Order* o = it->second;
        if (o->side == Side::Buy) remove_from(bids_, o);
        else                      remove_from(asks_, o);
        index_.erase(it);
        pool_.release(o);
        return true;
    }

    [[nodiscard]] bool best_bid(Price& out) const {
        if (bids_.empty()) return false;
        out = bids_.begin()->first;
        return true;
    }
    [[nodiscard]] bool best_ask(Price& out) const {
        if (asks_.empty()) return false;
        out = asks_.begin()->first;
        return true;
    }

    [[nodiscard]] Quantity qty_at(Side side, Price price) const {
        if (side == Side::Buy) {
            auto it = bids_.find(price);
            return it == bids_.end() ? 0 : it->second.total_qty;
        }
        auto it = asks_.find(price);
        return it == asks_.end() ? 0 : it->second.total_qty;
    }

    [[nodiscard]] std::size_t live_orders() const noexcept {
        return index_.size();
    }

private:
    // Bids: highest price first. Asks: lowest price first. Either way,
    // begin() is the best price on that side.
    using Bids = std::map<Price, PriceLevel, std::greater<Price>>;
    using Asks = std::map<Price, PriceLevel, std::less<Price>>;

    // Walk the opposite book, consuming liquidity while it crosses the taker's
    // limit. Returns the taker quantity left unfilled. Branch on TakerSide is
    // resolved at compile time (if constexpr) -> no runtime side check in loop.
    template <Side TakerSide, typename OnTrade>
    Quantity match(OrderId taker, Price limit, Quantity qty, OnTrade& on_trade) {
        if constexpr (TakerSide == Side::Buy) {
            auto it = asks_.begin();
            while (qty > 0 && it != asks_.end() && it->first <= limit) {
                qty = consume_level(taker, it->second, qty, it->first, on_trade);
                if (it->second.empty()) it = asks_.erase(it);
                else ++it;
            }
        } else {
            auto it = bids_.begin();
            while (qty > 0 && it != bids_.end() && it->first >= limit) {
                qty = consume_level(taker, it->second, qty, it->first, on_trade);
                if (it->second.empty()) it = bids_.erase(it);
                else ++it;
            }
        }
        return qty;
    }

    template <typename OnTrade>
    Quantity consume_level(OrderId taker, PriceLevel& level, Quantity qty,
                           Price px, OnTrade& on_trade) {
        while (qty > 0 && level.head) {
            Order*   maker = level.head;
            Quantity fill  = std::min(qty, maker->quantity);

            on_trade(Trade{taker, maker->id, px, fill});

            qty             -= fill;
            maker->quantity -= fill;
            level.total_qty -= fill;

            if (maker->quantity == 0) {
                level.head = maker->next;
                if (level.head) level.head->prev = nullptr;
                else            level.tail = nullptr;
                index_.erase(maker->id);
                pool_.release(maker);
            }
        }
        return qty;
    }

    template <typename Book>
    void rest(OrderId id, Side side, Price price, Quantity qty, Book& book) {
        Order* o   = pool_.acquire();
        o->id      = id;
        o->price   = price;
        o->quantity = qty;
        o->side    = side;
        o->prev = o->next = nullptr;
        book[price].push_back(o);
        index_[id] = o;
    }

    template <typename Book>
    void remove_from(Book& book, Order* o) {
        auto it = book.find(o->price);
        if (it != book.end()) {
            it->second.unlink(o);
            if (it->second.empty()) book.erase(it);
        }
    }

    Bids                                bids_;
    Asks                                asks_;
    ObjectPool<Order>                   pool_;
    std::unordered_map<OrderId, Order*> index_;  // id -> resting order
};

} // namespace ob
