// Minimal dependency-free test harness. Each TEST verifies one property of the
// matching engine. Build with `make test` (see Makefile) and run ./build/tests.

#include <cstdio>
#include <vector>

#include "orderbook/order_book.hpp"

using namespace ob;

static int g_failures = 0;
static int g_checks   = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// Collect trades for assertions.
struct Collector {
    std::vector<Trade> trades;
    void operator()(const Trade& t) { trades.push_back(t); }
};

static void test_rest_no_cross() {
    OrderBook book;
    Collector c;
    book.submit(1, Side::Buy, 100, 10, std::ref(c));   // rests, no ask to hit
    CHECK(c.trades.empty());
    Price bid;
    CHECK(book.best_bid(bid) && bid == 100);
    CHECK(book.live_orders() == 1);
}

static void test_full_fill() {
    OrderBook book;
    Collector c;
    book.submit(1, Side::Sell, 100, 10, std::ref(c)); // resting ask
    book.submit(2, Side::Buy,  100, 10, std::ref(c)); // crosses, fully fills
    CHECK(c.trades.size() == 1);
    CHECK(c.trades[0].price == 100);
    CHECK(c.trades[0].quantity == 10);
    CHECK(c.trades[0].maker_id == 1);
    CHECK(c.trades[0].taker_id == 2);
    CHECK(book.live_orders() == 0); // both gone
}

static void test_partial_fill_taker_rests() {
    OrderBook book;
    Collector c;
    book.submit(1, Side::Sell, 100, 4, std::ref(c));
    book.submit(2, Side::Buy,  100, 10, std::ref(c)); // takes 4, rests 6
    CHECK(c.trades.size() == 1);
    CHECK(c.trades[0].quantity == 4);
    Price bid;
    CHECK(book.best_bid(bid) && bid == 100);
    CHECK(book.qty_at(Side::Buy, 100) == 6);
}

static void test_price_time_priority() {
    OrderBook book;
    Collector c;
    // Two asks at the same price: id 1 arrived first, must fill first.
    book.submit(1, Side::Sell, 100, 5, std::ref(c));
    book.submit(2, Side::Sell, 100, 5, std::ref(c));
    book.submit(3, Side::Buy,  100, 7, std::ref(c)); // 5 from id1, 2 from id2
    CHECK(c.trades.size() == 2);
    CHECK(c.trades[0].maker_id == 1 && c.trades[0].quantity == 5);
    CHECK(c.trades[1].maker_id == 2 && c.trades[1].quantity == 2);
    CHECK(book.qty_at(Side::Sell, 100) == 3); // 3 left on id2
}

static void test_walk_multiple_levels() {
    OrderBook book;
    Collector c;
    book.submit(1, Side::Sell, 101, 5, std::ref(c));
    book.submit(2, Side::Sell, 100, 5, std::ref(c)); // better price -> hit first
    book.submit(3, Side::Buy,  101, 8, std::ref(c)); // 5@100 then 3@101
    CHECK(c.trades.size() == 2);
    CHECK(c.trades[0].price == 100 && c.trades[0].quantity == 5);
    CHECK(c.trades[1].price == 101 && c.trades[1].quantity == 3);
}

static void test_cancel() {
    OrderBook book;
    Collector c;
    book.submit(1, Side::Buy, 100, 10, std::ref(c));
    CHECK(book.cancel(1));
    CHECK(!book.cancel(1));      // already gone
    CHECK(book.live_orders() == 0);
    Price bid;
    CHECK(!book.best_bid(bid));  // book empty
}

static void test_no_cross_when_limit_not_met() {
    OrderBook book;
    Collector c;
    book.submit(1, Side::Sell, 101, 5, std::ref(c));
    book.submit(2, Side::Buy,  100, 5, std::ref(c)); // 100 < 101, no trade
    CHECK(c.trades.empty());
    CHECK(book.live_orders() == 2);
}

int main() {
    std::printf("running order book tests...\n");
    test_rest_no_cross();
    test_full_fill();
    test_partial_fill_taker_rests();
    test_price_time_priority();
    test_walk_multiple_levels();
    test_cancel();
    test_no_cross_when_limit_not_met();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
