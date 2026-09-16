#pragma once
// Deterministic fixed-point scalar for the simulation's positions.
//
// WHY. Lockstep requires every peer to compute byte-identical state. Float positions
// get there only by discipline: detmath for the transcendentals, -ffp-contract=off so
// no compiler fuses a*b+c differently, and a guard script that greps for stray libm
// calls. That is a rule maintained by vigilance, and it leaks -- a traced route's
// corner waypoint past a parked body is EXACTLY tangent, footprints touching at 32px
// with a margin of 0.000, and float drift of four hundredths of a pixel was enough to
// wedge a unit against it permanently. Integers do not drift.
//
// FORMAT. 16.16 in int32: 65536 units per pixel. That is retail's own resolution --
// its mover steps in 0x100000 per 16px cell, which is the same 65536 per pixel -- so
// a waypoint one cell clear of a body is an exact integer here, as it is there, and
// the tangency that needed a sub-pixel slack cannot arise.
//
// RANGE. +/-32768 px against a map that tops out near 4096. Products use an int64
// intermediate, so a multiply cannot overflow before it is shifted back down.
//
// CONVERSIONS ARE EXPLICIT, deliberately. An implicit float conversion would let a
// stray double creep back into the hashed path and be invisible; making every crossing
// spelled out is what lets the compiler find the sites during the port, and what keeps
// the boundary (rendering, FBI data, the AI's scratch maths) visible afterwards.

#include <cstdint>
#include <cmath>

namespace tak::sim {

struct Fixed {
    static constexpr int kBits = 16;
    static constexpr int32_t kOne = 1 << kBits;

    int32_t v = 0;

    constexpr Fixed() = default;
    static constexpr Fixed raw(int32_t r) { Fixed f; f.v = r; return f; }
    static constexpr Fixed fromInt(int i) { return raw(int32_t(i) << kBits); }
    // From float ONLY at a boundary: unit data read from FBI, the renderer, a test.
    // Never inside the tick. std::lround is round-half-away-from-zero and identical
    // everywhere, unlike a bare cast's truncation-toward-zero.
    static Fixed fromFloat(float f) { return raw(int32_t(std::lround(double(f) * kOne))); }

    constexpr float toFloat() const { return float(v) / float(kOne); }
    // Truncates toward NEGATIVE infinity, like a floor, so cell indexing is correct
    // left of the origin. A bare v/kOne would round toward zero and put x=-0.5 in
    // cell 0 alongside x=+0.5.
    constexpr int32_t floorInt() const { return v >> kBits; }

    constexpr Fixed operator+(Fixed o) const { return raw(v + o.v); }
    constexpr Fixed operator-(Fixed o) const { return raw(v - o.v); }
    constexpr Fixed operator-() const { return raw(-v); }
    // int64 intermediate, then an ARITHMETIC shift -- guaranteed since C++20, so the
    // rounding of a negative product is the same on every compiler. Truncates toward
    // negative infinity, consistently for both signs.
    constexpr Fixed operator*(Fixed o) const {
        return raw(int32_t((int64_t(v) * int64_t(o.v)) >> kBits));
    }
    constexpr Fixed operator/(Fixed o) const {
        return o.v == 0 ? raw(0) : raw(int32_t((int64_t(v) << kBits) / int64_t(o.v)));
    }
    constexpr Fixed operator*(int i) const { return raw(v * int32_t(i)); }
    constexpr Fixed operator/(int i) const { return i == 0 ? raw(0) : raw(v / int32_t(i)); }

    constexpr Fixed& operator+=(Fixed o) { v += o.v; return *this; }
    constexpr Fixed& operator-=(Fixed o) { v -= o.v; return *this; }

    constexpr bool operator==(Fixed o) const { return v == o.v; }
    constexpr bool operator!=(Fixed o) const { return v != o.v; }
    constexpr bool operator< (Fixed o) const { return v <  o.v; }
    constexpr bool operator<=(Fixed o) const { return v <= o.v; }
    constexpr bool operator> (Fixed o) const { return v >  o.v; }
    constexpr bool operator>=(Fixed o) const { return v >= o.v; }
};

constexpr Fixed operator*(int i, Fixed f) { return f * i; }
constexpr Fixed fxAbs(Fixed a) { return a.v < 0 ? -a : a; }
constexpr Fixed fxMin(Fixed a, Fixed b) { return a.v < b.v ? a : b; }
constexpr Fixed fxMax(Fixed a, Fixed b) { return a.v > b.v ? a : b; }

// Integer square root of a 64-bit value (Newton, seeded by bit length). Used by the
// length below; no float anywhere, so it cannot differ between builds.
constexpr uint64_t isqrt64(uint64_t n) {
    if (n == 0) return 0;
    uint64_t x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

// hypot in fixed point. The squares are computed in int64 at 32 fractional bits, so
// the sqrt of that is back at 16 -- no intermediate rounding, and no overflow until
// well beyond any map.
inline Fixed fxLen(Fixed a, Fixed b) {
    const int64_t aa = int64_t(a.v) * int64_t(a.v);
    const int64_t bb = int64_t(b.v) * int64_t(b.v);
    return Fixed::raw(int32_t(isqrt64(uint64_t(aa + bb))));
}

}  // namespace tak::sim
