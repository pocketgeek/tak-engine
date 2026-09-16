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
    // IMPLICIT TO FLOAT, AND THIS IS PORT SCAFFOLDING, not the end state.
    //
    // Converting Unit::x/z alone breaks 248 sites: 28 writes and the rest reads. The
    // writes are what decide stored precision, so those stay explicit and get reviewed
    // one at a time. The reads are float arithmetic that was float before this change
    // and is no worse for it, so letting them convert silently is what makes a port of
    // this size tractable at all.
    //
    // Be clear about what it does NOT buy: a float carries 24 bits of mantissa, and a
    // coordinate near the far edge of a map needs 28 at this resolution, so a read is
    // LOSSY out there. Determinism therefore arrives only where a path is converted to
    // Fixed end to end -- the mover and the collision test. Everywhere still reading
    // through here is exactly as portable as it was, which is to say it relies on
    // detmath and -ffp-contract=off. Each converted path should drop its reads; when
    // the last one goes, so does this operator.
    constexpr operator float() const { return toFloat(); }
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

// ---------------------------------------------------------------------------------
// Angles, and integer trig.
//
// A BINARY ANGLE: 65536 == 360 degrees, held in int32 and wrapped by truncation, so
// 0x4000 is a right angle. That is retail's own convention (heading at navigator +0x7e,
// with the mover comparing headings against 0x4000 for its 90-degree test), and it has
// the property radians never will -- adding two angles cannot drift, and wrapping is
// free rather than a fmod that has to agree across libms.
using Bam = int32_t;
constexpr Bam kBamFull = 65536, kBamHalf = 32768, kBamQuarter = 16384;

// CORDIC, because it is the only way to get sin and cos with no float anywhere. Sixteen
// rotations of shift-and-add drive the residual angle to zero; the table is atan(2^-i)
// in BAM and the constant is the accumulated gain. Both were computed in the open (see
// the generator in the commit message) rather than read out of the retail binary, which
// is for observation only.
//
// Accurate to about 1e-4 of full scale, which is finer than the 1/65536 px the result is
// stored at, and -- the point of the exercise -- IDENTICAL on every machine, because it
// is integer shifts and adds with no rounding mode to disagree about.
namespace detail {
constexpr int32_t kCordicAtan[16] = {
    8192, 4836, 2555, 1297, 651, 326, 163, 81,
    41, 20, 10, 5, 3, 1, 1, 0,
};
constexpr int32_t kCordicGain = 39797;   // 0.6072529351 in 16.16
}  // namespace detail

// sin and cos together: CORDIC produces both, and every caller that wants one wants the
// other a line later.
struct SinCos { Fixed s, c; };

inline SinCos fxSinCos(Bam a) {
    a &= (kBamFull - 1);                       // wrap: free, and exact
    // THE FOUR CARDINALS EXACTLY. CORDIC converges to about 6/65536 at 0 and 90, and
    // that residual is not harmless here: it is 0.0002px of sideways push per step, so
    // a unit ordered straight along an axis slides ~2px off its line over a five-minute
    // walk. Axis-aligned movement is the common case (waypoints sit on cell centres),
    // and drift is the entire reason positions became integers, so these are answered
    // outright rather than approximated.
    switch (a) {
        case 0:                         return {Fixed(), Fixed::fromInt(1)};
        case kBamQuarter:               return {Fixed::fromInt(1), Fixed()};
        case kBamHalf:                  return {Fixed(), -Fixed::fromInt(1)};
        case kBamHalf + kBamQuarter:    return {-Fixed::fromInt(1), Fixed()};
        default: break;
    }
    // Fold to the first quadrant and remember the signs, so the rotation only ever has
    // to cover 90 degrees where CORDIC converges.
    bool negS = false, negC = false;
    if (a >= kBamHalf) { a -= kBamHalf; negS = !negS; negC = !negC; }
    if (a >= kBamQuarter) { a = kBamHalf - a; negC = !negC; }
    int64_t x = detail::kCordicGain, y = 0;
    int32_t z = a;
    for (int i = 0; i < 16; ++i) {
        const int64_t dx = x >> i, dy = y >> i;
        if (z >= 0) { const int64_t nx = x - dy; y = y + dx; x = nx; z -= detail::kCordicAtan[i]; }
        else        { const int64_t nx = x + dy; y = y - dx; x = nx; z += detail::kCordicAtan[i]; }
    }
    SinCos r;
    r.s = Fixed::raw(int32_t(negS ? -y : y));
    r.c = Fixed::raw(int32_t(negC ? -x : x));
    return r;
}
inline Fixed fxSin(Bam a) { return fxSinCos(a).s; }
inline Fixed fxCos(Bam a) { return fxSinCos(a).c; }

// Boundary helpers: unit data, map files and the renderer all still speak radians.
inline Bam bamFromRadians(float r) {
    return Bam(std::lround(double(r) * (65536.0 / (2.0 * 3.14159265358979323846))))
           & (kBamFull - 1);
}
constexpr float radiansFromBam(Bam a) {
    return float(double(a) * (2.0 * 3.14159265358979323846 / 65536.0));
}

}  // namespace tak::sim
