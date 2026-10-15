#!/usr/bin/env python3
"""Compare AirToGround (41bf80), the attack runs of a flyer without
hoverattack, with retailAirToGround (src/sim/retailairattack.h).

Runs the original handler on random states of every stage: the approach
controller and its radius, the run past the target, the turn, the return
from off the map, the 15- and 1-tick sleeps, the counter at mission +0x4e,
the abort (0x2a) and restart (0x40) events and every random draw (535cc0,
native, state at 0x64186c). Controller allocation, construction, the radius
setter and installation are host stubs that record their arguments; the
installation stub clears the pending controller events as 4d4d40 does. The
group check (51d1e0) answers no group, and the air-traffic counter (unit
+0x12a) stays below the step-out threshold: neither is ported.

    python3 tools/re/check_air_attack_run.py [build-dbg/retail_motion_test]
"""
import random
import struct
import subprocess
import sys

from emu import Icd, HEAP
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_FPCW

RNG = 0x64186c


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else 'build-dbg/retail_motion_test'
    p = Icd()
    p.uc.reg_write(UC_X86_REG_FPCW, 0x027f)
    unit, mission, kind, weapon, target, target_kind, mover, offmap, sector, controller = \
        [HEAP + i * 0x1000 for i in range(10)]
    game = HEAP + 0x20000   # the game record reaches past +0x19f44

    def put(address, fmt, *values):
        p.uc.mem_write(address, struct.pack('<' + fmt, *values))

    def get(address, fmt='I'):
        return struct.unpack('<' + fmt, p.uc.mem_read(address, struct.calcsize(fmt)))[0]

    put(0x62d55c, 'I', game)
    put(game + 0x19f30, 'I', offmap)
    log = {}

    def record(name, value=None):
        log.setdefault(name, []).append(value)

    def allocate(uc, args):
        size = get(args)
        if size != 0x36:
            raise AssertionError('unexpected allocation of %#x bytes' % size)
        return 0, controller

    def construct(uc, args):   # 4e40e0(mission, point), thiscall
        if get(args) != mission:
            raise AssertionError('controller for another mission')
        p.uc.mem_write(controller + 0x26, bytes(uc.mem_read(get(args + 4), 12)))
        put(controller + 0xa, 'h', 0)
        record('construct')
        return 2, controller

    def radius(uc, args):      # 4e4540(radius), thiscall
        put(controller + 0xa, 'H', get(args) & 0xffff)
        return 1, 0

    def install(uc, args):     # 4d4d40(controller), thiscall on the mission
        record('install', get(args))
        put(mission + 0x6a, 'I', get(mission + 0x6a) & 0xffffc8ff)
        return 1, 0

    def unexpected(name):
        def hook(uc, args):
            raise AssertionError('unexpected call to ' + name)
        return hook

    p.hooks[0x51d1e0] = lambda uc, a: (3, 0)                       # no group
    p.hooks[0x4eb9e0] = allocate
    p.hooks[0x4e40e0] = construct
    p.hooks[0x4e4540] = radius
    p.hooks[0x4d4d40] = install
    p.hooks[0x416c50] = lambda uc, a: (record('begin'), (4, 0))[1]
    p.hooks[0x519b10] = lambda uc, a: (record('auto', get(a)), (1, 0))[1]
    p.hooks[0x519b70] = lambda uc, a: (record('manual', get(a)), (1, 0))[1]
    p.hooks[0x51a700] = lambda uc, a: (record('aim', (get(a), get(a + 4), get(a + 8))), (3, 0))[1]
    for address, name in [(0x51a770, 'ground aim'), (0x4d8370, 'target scan'), (0x4d7de0, 'attack'),
                          (0x4d4bf0, 'mission lookup'), (0x4d6c40, 'mission'), (0x4d7750, 'push'),
                          (0x4d77b0, 'insert'), (0x50c480, 'group leave')]:
        p.hooks[address] = unexpected(name)
    p.freeze_hooks()
    draws = [0]
    p.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, d: draws.__setitem__(0, draws[0] + 1),
                  begin=0x535cc0, end=0x535cc0)

    # A flyer that may attack (+0x130 0x8000000, alive), a canfly type
    # without hoverattack or weaponswitching, slot 0 not `dropped`; a live
    # target whose type cannot fly.
    put(unit + 0x130, 'I', 0x9000000)
    put(unit + 0xb4, 'I', kind)
    put(unit + 0x8, 'I', mover)
    put(unit + 0xc, 'I', weapon)
    put(unit + 0x28, 'I', 0)
    put(unit + 0x44, 'I', 0)
    put(unit + 0x12a, 'B', 0)
    put(kind + 0x260, 'I', 0x800)
    put(kind + 0x1aa, 'I', weapon)
    put(weapon + 0xc8, 'I', 0)
    put(target + 0x130, 'I', 0x1000001)
    put(target + 0x114, 'B', 0)
    put(target + 0xb4, 'I', target_kind)
    put(target_kind + 0x260, 'I', 0)

    rng = random.Random(0x41bf80)
    fixtures, expected = [], []
    s32 = lambda v: (v + 2**31) % 2**32 - 2**31
    for i in range(6000):
        stage = rng.choice((0, 1, 2, 3, 4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8))
        events = rng.choice((0, 0, 1, 1, 0x100, 0x400, 0x500, 0x200, 0x501, 0x700, 0x2, 0x8, 0x20, 0x40, 0x41, 0x4))
        tick = rng.randrange(1000000)
        deadline = rng.choice((0xffffffff, rng.randrange(2**32)))
        pending = rng.choice((0, 0x100, 0x500, 0x3700, 0xffff, rng.randrange(2**32)))
        flags = rng.choice((0x1000200, 0x1000200, 0x9000200, 0x8000000))
        count = rng.choice((-1, 0, 0, 1, 1, 2, 3, 5))
        width = rng.choice((4096 - 32, 8192 - 32, rng.randrange(256, 12000)))
        height = rng.choice((4096 - 128, 8192 - 128, rng.randrange(256, 12000)))
        x = rng.randrange(-600, width + 600) * 65536 + rng.randrange(65536)
        z = rng.randrange(-600, height + 600) * 65536 + rng.randrange(65536)
        y = rng.randrange(0, 512) * 65536 + rng.randrange(65536)
        weapon_range = rng.choice((2, 3, 140, 200, 450, 500, 510, rng.randrange(2, 2000)))
        spread = rng.choice((0, 16, 140, 600, 2500))
        tx = x + rng.randrange(-spread, spread + 1) * 65536 + rng.randrange(65536)
        tz = z + rng.randrange(-spread, spread + 1) * 65536 + rng.randrange(65536)
        ty = rng.randrange(0, 256) * 65536
        if i % 11 == 0:
            tx, tz = x, z
        elif stage == 5 and i % 3 == 0:
            # Stage 5's range test, at its boundary: exactly half the range.
            tx, tz = x - (weapon_range // 2) * 65536 + rng.choice((-1, 0, 0, 1)), z
        heading = rng.randrange(65536)
        run_length = rng.choice((0, 0, 1, 150, 200, 400, rng.randrange(0, 2000), rng.randrange(65536)))
        on_map = rng.random() < 0.6
        seed = rng.randrange(2**32)

        put(game + 0x19f44, 'I', tick)
        put(game + 0x19e90, 'I', width)
        put(game + 0x19e94, 'I', height)
        put(unit + 0x68, 'iii', s32(x), s32(y), s32(z))
        put(unit + 0x7e, 'H', heading)
        put(unit + 0xa4, 'I', sector if on_map else offmap)
        put(kind + 0x234, 'H', run_length)
        put(weapon + 0x90, 'i', weapon_range)
        put(target + 0x68, 'iii', s32(tx), s32(ty), s32(tz))
        put(mission + 5, 'B', stage)
        put(mission + 6, 'I', 0)
        put(mission + 0xa, 'I', deadline)
        put(mission + 0x16, 'I', target)
        put(mission + 0x4e, 'i', count)
        put(mission + 0x52, 'I', 0)
        put(mission + 0x56, 'I', 0)
        put(mission + 0x5a, 'I', flags)
        put(mission + 0x66, 'I', 0)
        put(mission + 0x6a, 'I', pending)
        put(controller + 0x26, 'iii', 0, 0, 0)
        put(controller + 0xa, 'h', 0)
        put(RNG, 'I', seed)
        log.clear()
        draws[0] = 0
        result, error = p.call(0x41bf80, (unit, mission, events))
        if error:
            raise AssertionError((i, error))
        installs = len(log.get('install', []))
        if installs and log['install'][-1] != controller:
            raise AssertionError((i, 'installed a controller it did not build'))
        # 519b70 and 519b10 read only the low byte of their slot argument.
        if log.get('manual', [0])[0] & 0xff != 0 or any(a & 0xff != 3 for a in log.get('auto', [])):
            raise AssertionError((i, 'slot calls', log))
        if log.get('aim') and log['aim'][0] != (unit, target, 0):
            raise AssertionError((i, 'aim', log['aim']))
        point = struct.unpack('<3i', p.uc.mem_read(controller + 0x26, 12)) if installs else (0, 0, 0)
        expected.append((result, get(mission + 5, 'B'), get(mission + 6), get(mission + 0xa),
                         get(mission + 0x6a), get(mission + 0x5a), get(mission + 0x4e, 'i'), installs,
                         *point, get(controller + 0xa, 'h') if installs else 0,
                         len(log.get('begin', [])), len(log.get('auto', [])), len(log.get('aim', [])),
                         get(RNG), draws[0]))
        fixtures.append(' '.join(map(str, (stage, events, tick, deadline, pending, flags, count,
                                           s32(x), s32(y), s32(z), s32(tx), s32(ty), s32(tz), heading,
                                           weapon_range, run_length, int(on_map), width, height, seed))))
    output = subprocess.run([binary, '--air-run'], input='\n'.join(fixtures) + '\n',
                            text=True, capture_output=True, check=True)
    actual = [tuple(map(int, line.split())) for line in output.stdout.splitlines()]
    if len(actual) != len(expected):
        raise AssertionError('row count %d, expected %d' % (len(actual), len(expected)))
    for i, (got, want) in enumerate(zip(actual, expected)):
        if got != want:
            raise AssertionError('fixture %d: %s\n  port   %s\n  native %s' % (i, fixtures[i], got, want))
    stages = {}
    for fixture, row in zip(fixtures, expected):
        stages[int(fixture.split()[0])] = stages.get(int(fixture.split()[0]), 0) + 1
    installed = sum(1 for row in expected if row[7])
    print('PASS: %d AirToGround calls (stages %s), %d controllers, every state, point, radius and draw'
          % (len(expected), ' '.join('%d:%d' % kv for kv in sorted(stages.items())), installed))


if __name__ == '__main__':
    main()
