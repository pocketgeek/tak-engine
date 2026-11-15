"""Where the flight ports leave retail on purpose, for the checks that feed them the whole 32-bit range.

A position is 16.16 in 32 bits. Retail adds and subtracts positions in 32-bit
registers, where a sum past the end of the range wraps, and it reads a
whole-pixel distance as a signed 16-bit word, which turns negative at
32768 px. No retail map is wide enough for either to happen. The ports stop at
the end of the range instead (satAdd32 / satSub32, src/sim/fixed.h) and read
the distance whole: docs/retail-engine.md, "Nothing bounds a position".

A check counts the fixtures that reach either and compares every other
fixture exactly.
"""
import math

TOP, BOTTOM = 2**31 - 1, -2**31


def saturated(got, want):
    """The port's row is the native one except where the port stopped at an end of the range."""
    return got != want and len(got) == len(want) and \
        all(g == w or g in (TOP, BOTTOM) for g, w in zip(got, want))


def fits(*raw):
    """Every raw coordinate (before any 32-bit wrap) lies inside the range."""
    return all(BOTTOM <= v <= TOP for v in raw)


def pixels(ax, az, bx, bz):
    """The whole-pixel distance between two raw points."""
    return math.isqrt((ax - bx)**2 + (az - bz)**2) >> 16
