// fixed_test -- the arithmetic the sim's positions will be built on.
//
// This is the net for a conversion that touches ~240 sites of HASHED state, where a
// single wrong shift is a desync rather than a visible bug. Every property here is one
// the port depends on: exact representation of the values routes are built from,
// rounding that matches on both signs, cell indexing that floors rather than truncates
// toward zero, and a multiply that cannot overflow before it is shifted back.
//
// The tangency case has its own check, because it is the reason this type exists: a
// unit one cell clear of a body must be EXACTLY clear, with no drift to wedge on.

#include "sim/fixed.h"

#include <cstdio>
#include <cstdint>
#include <string>

using namespace tak::sim;

static int failures = 0;
static void check(bool ok, const std::string& what, const std::string& detail = {}) {
    std::printf("  %-58s %s%s%s\n", what.c_str(), ok ? "ok" : "FAIL",
                detail.empty() ? "" : " -- ", detail.c_str());
    if (!ok) ++failures;
}

int main() {
    std::printf("fixed_test -- deterministic 16.16 for sim positions\n");

    // --- exactness of the values positions actually take ---------------------
    check(Fixed::fromInt(568).toFloat() == 568.0f, "whole pixels are exact");
    check((Fixed::fromInt(600) - Fixed::fromInt(568)).toFloat() == 32.0f,
          "a 32px footprint gap is exact (the tangency case)");
    // Half a cell, a quarter, an eighth: all the fractions a 16px grid produces.
    check((Fixed::fromInt(1) / 2).toFloat() == 0.5f,   "1/2 px exact");
    check((Fixed::fromInt(1) / 16).toFloat() == 0.0625f, "1/16 px exact");
    check((Fixed::fromInt(16) / 16).toFloat() == 1.0f, "one cell / 16 is one pixel");

    // --- the wedge this type exists to prevent -------------------------------
    {
        // The route's corner waypoint, one cell clear of a parked body. In float the
        // unit arrived at 568.04 and sat 0.04px inside, refused for ever.
        const Fixed ux = Fixed::fromInt(568), ox = Fixed::fromInt(600);
        const Fixed sep = Fixed::fromInt(32);
        const Fixed margin = sep - fxAbs(ux - ox);
        check(margin.v == 0, "tangent waypoint has EXACTLY zero margin, not 0.04px",
              "margin raw=" + std::to_string(margin.v));
        // ...and stays exact after a round trip through arithmetic, which is what the
        // mover does to it every tick.
        Fixed acc = ux;
        for (int i = 0; i < 1000; ++i) { acc += Fixed::fromInt(1); acc -= Fixed::fromInt(1); }
        check(acc == ux, "1000 add/subtract round trips do not drift");
    }

    // --- rounding is the same on both signs ----------------------------------
    {
        const Fixed a = Fixed::fromInt(3) / 2;     // +1.5
        const Fixed b = -(Fixed::fromInt(3) / 2);  // -1.5
        check(a.floorInt() == 1,  "floorInt(+1.5) == 1");
        check(b.floorInt() == -2, "floorInt(-1.5) == -2 (floors, not truncates)",
              "got " + std::to_string(b.floorInt()));
        // A cell index must not fold -0.5 and +0.5 into the same cell.
        check(Fixed::raw(-1).floorInt() == -1, "the smallest negative is cell -1, not 0");
    }

    // --- multiply: int64 intermediate, no overflow before the shift ----------
    {
        const Fixed big = Fixed::fromInt(4000);          // beyond any map edge
        const Fixed half = Fixed::fromInt(1) / 2;
        check((big * half).toFloat() == 2000.0f, "4000 * 0.5 == 2000 (no overflow)");
        const Fixed step = Fixed::fromInt(70) / 30;      // 70px/s at 30Hz
        check(step.v > 0 && step.toFloat() > 2.3f && step.toFloat() < 2.34f,
              "a 70px/s step at 30Hz lands near 2.333px",
              std::to_string(step.toFloat()));
    }

    // --- length, integer-only ------------------------------------------------
    {
        check(fxLen(Fixed::fromInt(3), Fixed::fromInt(4)) == Fixed::fromInt(5),
              "3,4,5 triangle is exact");
        check(fxLen(Fixed::fromInt(0), Fixed::fromInt(0)) == Fixed(), "len(0,0) == 0");
        const Fixed d = fxLen(Fixed::fromInt(2560), Fixed::fromInt(2560));
        check(d.toFloat() > 3620.0f && d.toFloat() < 3622.0f,
              "a full map diagonal is in range and sane", std::to_string(d.toFloat()));
    }

    // --- isqrt agrees with the real thing over a wide sweep ------------------
    {
        int bad = 0;
        for (uint64_t n = 0; n < 4000000; n += 997) {
            const uint64_t r = isqrt64(n);
            if (r * r > n || (r + 1) * (r + 1) <= n) ++bad;
        }
        check(bad == 0, "isqrt64 is the exact integer square root over 4014 samples",
              bad ? std::to_string(bad) + " wrong" : "");
    }

    std::printf(failures ? "fixed_test: FAILURES\n" : "fixed_test: all passed\n");
    return failures ? 1 : 0;
}
