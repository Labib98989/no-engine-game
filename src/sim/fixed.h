// Q16.16 fixed-point — the only numeric type for positions/distances in /sim.
// research.md §3.4. No float anywhere in this library; the build enforces it
// by linking neg_tests / neg_headless against neg_sim alone.
#pragma once
#include <cstdint>

namespace neg {

struct Fixed {
    int32_t v = 0;

    static constexpr int32_t ONE = 1 << 16;

    static constexpr Fixed from_int(int32_t i) { return Fixed{i * ONE}; }
    static constexpr Fixed from_raw(int32_t r) { return Fixed{r}; }
    static constexpr Fixed zero() { return Fixed{0}; }

    constexpr int32_t to_int() const { return v >> 16; } // floor

    constexpr Fixed operator+(Fixed o) const { return Fixed{v + o.v}; }
    constexpr Fixed operator-(Fixed o) const { return Fixed{v - o.v}; }
    constexpr Fixed operator-() const { return Fixed{-v}; }
    constexpr Fixed operator*(Fixed o) const {
        return Fixed{(int32_t)(((int64_t)v * o.v) >> 16)};
    }
    constexpr Fixed operator/(Fixed o) const {
        return Fixed{(int32_t)(((int64_t)v << 16) / o.v)};
    }

    Fixed& operator+=(Fixed o) { v += o.v; return *this; }
    Fixed& operator-=(Fixed o) { v -= o.v; return *this; }

    constexpr bool operator<(Fixed o) const { return v < o.v; }
    constexpr bool operator>(Fixed o) const { return v > o.v; }
    constexpr bool operator<=(Fixed o) const { return v <= o.v; }
    constexpr bool operator>=(Fixed o) const { return v >= o.v; }
    constexpr bool operator==(Fixed o) const { return v == o.v; }
    constexpr bool operator!=(Fixed o) const { return v != o.v; }

    // Exact division by a tick count (the per-tick slide step, technical.md §2).
    static constexpr Fixed div_int(Fixed a, int32_t n) { return Fixed{a.v / n}; }
    static constexpr Fixed abs(Fixed a) { return a.v < 0 ? Fixed{-a.v} : a; }
    static constexpr Fixed min(Fixed a, Fixed b) { return a.v < b.v ? a : b; }
    static constexpr Fixed max(Fixed a, Fixed b) { return a.v > b.v ? a : b; }
    static constexpr Fixed clamp(Fixed x, Fixed lo, Fixed hi) {
        return x.v < lo.v ? lo : (x.v > hi.v ? hi : x);
    }
};

} // namespace neg
