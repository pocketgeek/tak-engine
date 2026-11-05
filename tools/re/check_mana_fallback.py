#!/usr/bin/env python3
"""Compare the post-shot mana fallback (52ae90 52af6c..52afa1 -> 51a8b0) with
World's release (fire, script-triggered) and manaFallback (src/sim/sim.cpp).

52ae90 runs natively on a slot whose record holds SET 23 (flag 0x10): the
release 530220 with its payment 4010d0, the cost 531ea0 and the veteran
multiplier 5193c0, then the fallback, 51a8b0 and 51a7f0. Hooked: aim start
52fe30 (admits), readiness 52fff0 (not ready, so no new FireWeapon), the shot
allocation 529a90 (a buffer), the weapon class vtable (+0x2c create, the rest
false) and the script callbacks 56c640. Random selectors (3 = every slot, a
unit without weaponswitching), costs, mana, +0xf4 multipliers and noveteran;
the port prints the selector and the mana left (1/1024 units) for each.
+0xf4 is never 0 here: 5193c0's level path (1 + 0.1 x level) reads state
this fixture does not build, and World's vetMul already ports it.

    python3 tools/re/check_mana_fallback.py [build-o2/retail_script_test]
"""
import random
import struct
import subprocess
import sys

from emu import Icd, HEAP
from unicorn.x86_const import UC_X86_REG_FPCW


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else 'build-o2/retail_script_test'
    p = Icd()
    slot_at = lambda i: HEAP + i * 0x8000
    game, unit, kind, script, shot, vt, cls = (slot_at(i) for i in range(7))
    weapons = [slot_at(7), slot_at(8), slot_at(9)]
    units = slot_at(10)
    stub_create, stub_false = slot_at(11), slot_at(11) + 0x10

    def put(address, value, fmt='I'):
        p.uc.mem_write(address, struct.pack('<' + fmt, value))

    def get(address, fmt='I'):
        return struct.unpack('<' + fmt, p.uc.mem_read(address, struct.calcsize(fmt)))[0]

    p.hooks.update({
        0x52fe30: lambda u, s: (2, 1),
        0x52fff0: lambda u, s: (2, 0),
        0x529a90: lambda u, s: (0, shot),
        stub_create: lambda u, s: (3, 0),
        stub_false: lambda u, s: (0, 0),
        0x56c640: lambda u, s: (8, 0),
        0x4e8030: lambda u, s: (0, 0)})
    p.freeze_hooks()
    put(0x62d55c, game); put(game + 0x19f44, 5000); put(game + 0x3070, 0, 'B')
    put(game + 0x14e84, units); put(game + 0x14e88, units + 0x138 * 50)
    for i in range(0, 0x100, 4):
        put(vt + i, stub_false)
    put(vt + 0x2c, stub_create); put(cls, vt)

    def native(selected, costs, mana, vet, noveteran):
        p.uc.mem_write(unit, bytes(0x138)); p.uc.mem_write(kind, bytes(0x300)); p.uc.mem_write(shot, bytes(0x100))
        put(unit + 0xb4, kind); put(unit + 0xbc, script); put(unit + 0xb8, 0)
        put(kind + 0x260, 0x10000); put(kind + 0x268, 0x10 if noveteran else 0, 'B')
        put(unit + 0x130, 0x01000000 | (selected << 30)); put(unit + 0xf4, vet, 'f'); put(unit + 0xd8, mana, 'f')
        for s in range(3):
            w = weapons[s]; p.uc.mem_write(w, bytes(0x100))
            put(w + 0xd4, float(costs[s]), 'f'); put(w + 0x40, cls); put(w + 0x9e, 100 + s, 'H')
            put(kind + 0x1aa + 4 * s, w)
            record = unit + 0xc + 0x1c * s
            put(record, w); put(record + 4, 0, 'H'); put(record + 6, 0x8000, 'H')
            put(record + 0x14, 0, 'H')
            put(record + 0x1a, 0x10 if selected in (s, 3) else 0, 'H')
        put(unit + 0xd0, 0)
        p.uc.reg_write(UC_X86_REG_FPCW, 0x027f)
        _, error = p.call(0x52ae90, (unit,))
        assert not error, error
        return get(unit + 0x130) >> 30, round(get(unit + 0xd8, 'f') * 1024)

    rng = random.Random(0x52af72)
    fixtures = [
        (2, (0, 10, 30), 40, 1.0, 0), (2, (0, 10, 30), 35, 1.0, 0), (2, (0, 10, 30), 60, 1.0, 0),
        (2, (0, 10, 30), 59.5, 1.0, 0), (2, (0, 10, 30), 25, 1.0, 0), (2, (5, 50, 30), 40, 1.0, 0),
        (2, (50, 50, 30), 40, 1.0, 0), (1, (50, 30, 0), 40, 1.0, 0), (0, (30, 0, 0), 40, 1.0, 0),
        (1, (0, 10, 30), 15, 1.0, 0), (2, (0, 0, 0), 0, 1.0, 0), (3, (0, 10, 30), 40, 1.0, 0),
        (2, (0, 10, 30), 40, 2.0, 0), (2, (0, 10, 30), 40, 2.0, 1)]
    def cost():
        return rng.choice((0, 0, 5, 10, 12.5, 30, 50, rng.randrange(0, 240) / 4))
    for _ in range(600):
        fixtures.append((rng.choice((0, 1, 2, 2, 3)), (cost(), cost(), cost()), rng.randrange(0, 400) / 4,
                         rng.choice((1.0, 1.0, 1.3, 1.5, 2.0)), rng.choice((0, 0, 1))))
    expected = [native(*f) for f in fixtures]
    rows = ['%d %r %r %r %r %r %d' % (f[0], *map(float, f[1]), float(f[2]), f[3], f[4]) for f in fixtures]
    output = subprocess.run([binary, '--mana-fallback'], input='\n'.join(rows) + '\n',
                            text=True, capture_output=True, check=True)
    actual = [tuple(map(int, line.split())) for line in output.stdout.splitlines()]
    if len(actual) != len(expected):
        raise AssertionError('row count %d, expected %d' % (len(actual), len(expected)))
    switched = 0
    for i, (got, want) in enumerate(zip(actual, expected)):
        if got != want:
            raise AssertionError('fixture %d: %s\n  port   %s\n  native %s' % (i, fixtures[i], got, want))
        switched += want[0] != fixtures[i][0]
    print('52af72 mana fallback: %d fixtures match (%d switched)' % (len(fixtures), switched))


if __name__ == '__main__':
    main()
