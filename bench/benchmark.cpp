// Benchmark harness for the matching engine.
//
// Methodology notes (worth knowing for interviews):
//   * All inputs are pre-generated before timing, so the timed region contains
//     only engine work -- no RNG, no allocation, no I/O.
//   * Throughput is measured over the whole batch (amortized, no per-op timer).
//   * Latency percentiles time each submit() individually with steady_clock.
//     We first measure the clock's own overhead and report it, because at the
//     nanosecond scale the timer is part of what you observe. On real hardware
//     you would use rdtsc/rdtscp (serialized, calibrated to TSC frequency) to
//     shave that overhead; steady_clock is used here for portability.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include "orderbook/order_book.hpp"

using namespace ob;
using Clock = std::chrono::steady_clock;

namespace {

struct Op {
    OrderId  id;
    Side     side;
    Price    price;
    Quantity qty;
};

// Measure the floor cost of two back-to-back clock reads (the timer overhead
// that is unavoidably included in every per-op latency sample below).
std::uint64_t measure_clock_overhead_ns() {
    constexpr int N = 200000;
    std::vector<std::uint64_t> s;
    s.reserve(N);
    for (int i = 0; i < N; ++i) {
        auto a = Clock::now();
        auto b = Clock::now();
        s.push_back((std::uint64_t)std::chrono::duration_cast<
                        std::chrono::nanoseconds>(b - a).count());
    }
    std::sort(s.begin(), s.end());
    return s[s.size() / 2];  // median
}

template <typename T>
T pct(std::vector<T>& v, double p) {
    auto idx = (std::size_t)(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + idx, v.end());
    return v[idx];
}

} // namespace

int main() {
    constexpr int N = 1'000'000;

    // ---- pre-generate a realistic order flow ------------------------------
    // Prices cluster around a 10000 mid with a +/-100 band, so buys and sells
    // overlap and produce a steady stream of fills (not just inserts).
    std::mt19937_64 rng(0xC0FFEE);
    std::uniform_int_distribution<int>      price_d(9950, 10050);
    std::uniform_int_distribution<int>      qty_d(1, 50);
    std::uniform_int_distribution<int>      side_d(0, 1);

    std::vector<Op> ops;
    ops.reserve(N);
    for (int i = 0; i < N; ++i) {
        ops.push_back(Op{
            (OrderId)(i + 1),
            side_d(rng) ? Side::Sell : Side::Buy,
            (Price)price_d(rng),
            (Quantity)qty_d(rng)});
    }

    const std::uint64_t clk = measure_clock_overhead_ns();

    // ---- throughput run (no per-op timer) ---------------------------------
    {
        OrderBook book(1u << 18);
        std::uint64_t trades = 0;
        auto on_trade = [&](const Trade&) noexcept { ++trades; };

        auto t0 = Clock::now();
        for (const auto& o : ops)
            book.submit(o.id, o.side, o.price, o.qty, on_trade);
        auto t1 = Clock::now();

        double ns = (double)std::chrono::duration_cast<
                        std::chrono::nanoseconds>(t1 - t0).count();
        std::printf("== throughput ==\n");
        std::printf("orders submitted : %d\n", N);
        std::printf("trades generated : %llu\n", (unsigned long long)trades);
        std::printf("resting at end   : %zu\n", book.live_orders());
        std::printf("total time       : %.2f ms\n", ns / 1e6);
        std::printf("throughput       : %.2f M orders/sec\n", N / ns * 1e3);
        std::printf("mean latency     : %.1f ns/order\n\n", ns / N);
    }

    // ---- latency run (per-op timing -> percentiles) -----------------------
    {
        OrderBook book(1u << 18);
        std::uint64_t trades = 0;
        auto on_trade = [&](const Trade&) noexcept { ++trades; };

        std::vector<std::uint32_t> lat;
        lat.reserve(N);
        for (const auto& o : ops) {
            auto a = Clock::now();
            book.submit(o.id, o.side, o.price, o.qty, on_trade);
            auto b = Clock::now();
            lat.push_back((std::uint32_t)std::chrono::duration_cast<
                              std::chrono::nanoseconds>(b - a).count());
        }

        std::printf("== submit() latency (per order, incl. ~%lluns timer) ==\n",
                    (unsigned long long)clk);
        std::printf("p50              : %u ns\n", pct(lat, 0.50));
        std::printf("p90              : %u ns\n", pct(lat, 0.90));
        std::printf("p99              : %u ns\n", pct(lat, 0.99));
        std::printf("p99.9            : %u ns\n", pct(lat, 0.999));
        std::printf("max              : %u ns\n",
                    *std::max_element(lat.begin(), lat.end()));
    }

    return 0;
}
