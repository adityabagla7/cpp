// A tiny end-to-end demo: build a book, cross a few orders, print the trades
// and resulting top of book. Run with `make demo && ./build/demo`.

#include <cstdio>

#include "orderbook/order_book.hpp"

using namespace ob;

int main() {
    OrderBook book;

    auto print_trade = [](const Trade& t) {
        std::printf("  TRADE  taker=%llu  maker=%llu  %llu @ %lld\n",
                    (unsigned long long)t.taker_id,
                    (unsigned long long)t.maker_id,
                    (unsigned long long)t.quantity,
                    (long long)t.price);
    };

    std::printf("seeding resting liquidity...\n");
    book.submit(1, Side::Sell, 10100, 50, print_trade);
    book.submit(2, Side::Sell, 10050, 30, print_trade);
    book.submit(3, Side::Buy,   9950, 40, print_trade);
    book.submit(4, Side::Buy,   9900, 60, print_trade);

    Price bid = 0, ask = 0;
    if (book.best_bid(bid) && book.best_ask(ask))
        std::printf("top of book: bid %lld x%llu  |  ask %lld x%llu\n",
                    (long long)bid, (unsigned long long)book.qty_at(Side::Buy, bid),
                    (long long)ask, (unsigned long long)book.qty_at(Side::Sell, ask));

    std::printf("\naggressive buy 70 @ 10100 (should sweep both asks):\n");
    book.submit(5, Side::Buy, 10100, 70, print_trade);

    std::printf("\nremaining after sweep:\n");
    if (book.best_ask(ask))
        std::printf("  best ask now %lld\n", (long long)ask);
    else
        std::printf("  ask side empty\n");
    std::printf("  bid 10100 resting qty: %llu\n",
                (unsigned long long)book.qty_at(Side::Buy, 10100));
    std::printf("  live orders: %zu\n", book.live_orders());

    return 0;
}
