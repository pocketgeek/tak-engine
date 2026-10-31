#!/usr/bin/env python3
"""Compare retail's air traffic with src/sim/retailairtraffic.h and
retailAirToGroundHover (src/sim/retailairattack.h).

1. The counter step (51dc4b..51dce4), run natively over the loop body for one
   live, not dying flyer, against retailAirTrafficStep for every counter and
   overlap count (65,536 pairs); a dead, a dying and a grounded unit's counter
   is left alone (the World caller's gates).
2. VTOL_StepOut (4214b0), the handler of all three step-outs, on random states
   of both stages and the head: the octant scores (eight draws) against up to
   seven overlapping flyers (41d780, live or dead, in an entity table of
   stride 0x138), the distance draws, the angle, the point controller and its
   radius, the off-map return, the mover and canfly exits. Controller
   allocation, construction, the radius setter and installation are stubs
   that record their arguments; installation clears the pending controller
   events as 4d4d40 does.
3. AirToGroundHover (41d820) on random states of every stage and the head
   past its target checks: the lost-target exit (events 0x28), the off-map
   return, the restart (0x40), the group check (51d1e0, answered) with the
   leave (50c480) and the VTOL_Move_Formation push, the flag's rand(25);
   stage 0's take-off, stage 1's approach controller (D + 144) or advance,
   stage 2's slot calls and led SweetSpot, stage 3's ring point, the target
   controller, its radius and the 15..29-tick sleep, stage 4's step-out (the
   count, the auto retarget, D, the give-up) and wasted-shot reposition, and
   stage 5's random ring point. The slot calls, the scan (4d8370), the attack
   (4d7de0), the mission lookups, constructors and pushes (4d4bf0, 4d6c40,
   4d7750, 4d77b0) are stubs that answer as the fixture says and record their
   arguments; the sleeps (4d6a10), _ftol and every draw (535cc0, state at
   0x64186c) are native.

    python3 tools/re/check_air_traffic.py [build-dbg/retail_motion_test]
"""
import random
import struct
import subprocess
import sys

from emu import Icd, HEAP
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_FPCW, UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EDI, \
    UC_X86_REG_EDX, UC_X86_REG_ECX

RNG = 0x64186c


def s32(v):
    return (v + 2**31) % 2**32 - 2**31


def run(binary, mode, lines):
    output = subprocess.run([binary, mode], input='\n'.join(lines) + '\n', text=True, capture_output=True, check=True)
    return [tuple(map(int, line.split())) for line in output.stdout.splitlines()]


def compare(name, fixtures, expected, actual):
    if len(actual) != len(expected):
        raise AssertionError('%s: row count %d, expected %d' % (name, len(actual), len(expected)))
    for i, (got, want) in enumerate(zip(actual, expected)):
        if got != want:
            raise AssertionError('%s fixture %d: %s\n  port   %s\n  native %s' % (name, i, fixtures[i], got, want))


def check_counter(binary):
    p = Icd()
    unit, kind, game = HEAP, HEAP + 0x1000, HEAP + 0x20000

    def put(a, fmt, *v):
        p.uc.mem_write(a, struct.pack('<' + fmt, *v))

    def get(a, fmt):
        return struct.unpack('<' + fmt, p.uc.mem_read(a, struct.calcsize(fmt)))[0]

    put(0x62d55c, 'I', game)
    put(unit + 0xb4, 'I', kind)

    def native(counter, overlaps, flags=0x1000000, canfly=True):
        put(unit + 0x130, 'I', flags)
        put(kind + 0x260, 'I', 0x800 if canfly else 0)
        put(unit + 0x12a, 'b', counter)
        put(unit + 0x117, 'b', overlaps)
        # One pass of the per-player loop body: the unit is the range's last.
        p.uc.reg_write(UC_X86_REG_EAX, unit)
        p.uc.reg_write(UC_X86_REG_EDI, unit)
        p.uc.reg_write(UC_X86_REG_EBX, 0x800)
        p.uc.reg_write(UC_X86_REG_EDX, game)
        p.uc.emu_start(0x51dc4b, 0x51dcf7, count=200)
        return get(unit + 0x12a, 'b')

    pairs = [(c, o) for c in range(-128, 128) for o in range(-128, 128)]
    expected = [native(c, o) for c, o in pairs]
    fixtures = ['%d %d' % pair for pair in pairs]
    actual = [row[0] for row in run(binary, '--air-traffic', fixtures)]
    for i, (got, want) in enumerate(zip(actual, expected)):
        if got != want:
            raise AssertionError('counter %s: port %d, native %d' % (fixtures[i], got, want))
    # The gates are the caller's: a dead (no 0x1000000), dying (0x1000) or
    # grounded type's counter is not touched.
    for flags, canfly in ((0, True), (0x1001000, True), (0x1000000, False)):
        if native(7, 3, flags, canfly) != 7:
            raise AssertionError('counter gate %#x canfly=%d' % (flags, canfly))
    print('PASS: air-traffic counter, %d counter/overlap pairs and the three gates' % len(pairs))


def check_step_out(binary):
    p = Icd()
    p.uc.reg_write(UC_X86_REG_FPCW, 0x027f)
    game = HEAP + 0x20000
    entities = HEAP + 0x40000          # stride 0x138
    mission, kind, offmap, controller = HEAP, HEAP + 0x1000, HEAP + 0x2000, HEAP + 0x3000

    def put(a, fmt, *v):
        p.uc.mem_write(a, struct.pack('<' + fmt, *v))

    def get(a, fmt='I'):
        return struct.unpack('<' + fmt, p.uc.mem_read(a, struct.calcsize(fmt)))[0]

    put(0x62d55c, 'I', game)
    put(game + 0x19f30, 'I', offmap)
    put(game + 0x14e84, 'I', entities)
    put(game + 0x14e88, 'I', entities + 0x138 * 40)
    log = {}

    def record(name, value=None):
        log.setdefault(name, []).append(value)

    def allocate(uc, args):
        if get(args) != 0x36:
            raise AssertionError('unexpected allocation of %#x bytes' % get(args))
        return 0, controller

    def construct(uc, args):   # 4e40e0(mission, point), thiscall
        if get(args) != mission:
            raise AssertionError('controller for another mission')
        p.uc.mem_write(controller + 0x26, bytes(uc.mem_read(get(args + 4), 12)))
        put(controller + 0xa, 'h', 0)
        put(controller + 8, 'H', 0x20)
        record('construct')
        return 2, controller

    def radius(uc, args):      # 4e4540(radius), thiscall
        put(controller + 0xa, 'H', get(args) & 0xffff)
        put(controller + 8, 'H', get(controller + 8, 'H') | 0x10)
        return 1, 0

    def install(uc, args):     # 4d4d40(controller), thiscall on the mission
        record('install', get(args))
        put(mission + 0x6a, 'I', get(mission + 0x6a) & 0xffffc8ff)
        return 1, 0

    p.hooks[0x4eb9e0] = allocate
    p.hooks[0x4e40e0] = construct
    p.hooks[0x4e4540] = radius
    p.hooks[0x4d4d40] = install
    p.freeze_hooks()
    draws = [0]
    p.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, d: draws.__setitem__(0, draws[0] + 1), begin=0x535cc0, end=0x535cc0)

    def entity(i):
        return entities + 0x138 * i

    rng = random.Random(0x4214b0)
    fixtures, expected = [], []
    stages = {}
    for i in range(3000):
        stage = rng.choice((0, 0, 0, 0, 0, 1, 1, 2))
        events = rng.choice((0, 0x100, 0x200, 0x400, 0x500, 1))
        tick = rng.randrange(1000000)
        deadline = rng.choice((0xffffffff, rng.randrange(2**32)))
        pending = rng.choice((0, 0x100, 0x500, 0x3700, rng.randrange(2**32)))
        flags = rng.choice((0x1000000, 0x5000000, 0))
        width = rng.choice((4096 - 32, 8192 - 32, rng.randrange(256, 12000)))
        height = rng.choice((4096 - 128, 8192 - 128, rng.randrange(256, 12000)))
        on_map = rng.random() < 0.85
        flies = rng.random() < 0.95
        x = rng.randrange(-300, width + 300) * 65536 + rng.randrange(65536)
        z = rng.randrange(-300, height + 300) * 65536 + rng.randrange(65536)
        y = rng.randrange(0, 512) * 65536 + rng.randrange(65536)
        heading = rng.randrange(65536)
        foot = rng.choice((1, 2, 3, 4, 6))
        distance = rng.choice(((rng.randrange(15) + foot) * 16,
                               (rng.randrange(10) + rng.randrange(10) + rng.randrange(10) + foot) * 16,
                               rng.randrange(0, 40)))
        point = (s32(rng.randrange(2**32)), s32(rng.randrange(2**32)), s32(rng.randrange(2**32)))
        seed = rng.randrange(2**32)
        n = rng.choice((0, 1, 1, 2, 3, 5, 7, -1))
        # The unit is entity 1; its listed overlaps are entities 2.. (some dead).
        me = entity(1)
        put(me + 0x68, 'iii', s32(x), s32(y), s32(z))
        put(me + 0x7e, 'H', heading)
        put(me + 0xb4, 'I', kind)
        put(me + 8, 'I', 1 if flies or rng.random() < 0.5 else 0)
        mover = get(me + 8)
        put(kind + 0x260, 'I', 0x800 if (flies or not mover) else 0)
        flies_port = bool(mover) and bool(get(kind + 0x260) & 0x800)
        put(me + 0xa4, 'I', offmap + 4 if on_map else offmap)
        put(me + 0x117, 'b', n)
        live = []
        for k in range(max(n, 0)):
            e = entity(2 + k)
            ox = x + rng.randrange(-48, 49) * 65536 + rng.randrange(65536)
            oz = z + rng.randrange(-48, 49) * 65536 + rng.randrange(65536)
            oy = rng.randrange(0, 512) * 65536
            alive = rng.random() < 0.85
            put(e + 0x68, 'iii', s32(ox), s32(oy), s32(oz))
            put(e + 0x130, 'I', 0x1000000 | (0x1000 if rng.random() < 0.1 else 0) if alive else 0)
            put(me + 0x118 + 2 * k, 'H', 2 + k)
            if alive:
                live.append((s32(ox), s32(oy), s32(oz)))
        put(game + 0x19f44, 'I', tick)
        put(game + 0x19e90, 'I', width)
        put(game + 0x19e94, 'I', height)
        put(mission + 5, 'B', stage)
        put(mission + 6, 'I', 0)
        put(mission + 0xa, 'I', deadline)
        put(mission + 0x22, 'iii', *point)
        put(mission + 0x4e, 'i', distance)
        put(mission + 0x5a, 'I', flags)
        put(mission + 0x6a, 'I', pending)
        put(controller + 0x26, 'iii', 0, 0, 0)
        put(controller + 0xa, 'h', 0)
        put(controller + 8, 'H', 0)
        put(RNG, 'I', seed)
        log.clear()
        draws[0] = 0
        result, error = p.call(0x4214b0, (me, mission, events))
        if error:
            raise AssertionError((i, error))
        installs = len(log.get('install', []))
        goal = struct.unpack('<3i', p.uc.mem_read(controller + 0x26, 12)) if installs else (0, 0, 0)
        expected.append((result, get(mission + 5, 'B'), get(mission + 6), get(mission + 0xa), get(mission + 0x6a),
                         get(mission + 0x5a), *struct.unpack('<3i', p.uc.mem_read(mission + 0x22, 12)), installs,
                         *goal, get(controller + 0xa, 'h') if installs else 0,
                         get(controller + 8, 'H') if installs else 0x20, get(RNG), draws[0]))
        fields = [stage, events, tick, deadline, pending, flags, *point, s32(x), s32(y), s32(z), heading, distance,
                  int(flies_port), int(on_map), width, height, seed, len(live)]
        for o in live:
            fields += list(o)
        fixtures.append(' '.join(map(str, fields)))
        key = 'off' if not on_map else stage
        stages[key] = stages.get(key, 0) + 1
    compare('step-out', fixtures, expected, run(binary, '--step-out', fixtures))
    print('PASS: VTOL_StepOut, %d calls (%s), every state, point, controller, radius and draw'
          % (len(fixtures), ' '.join('%s:%d' % kv for kv in sorted(stages.items(), key=str))))


def check_hover(binary):
    p = Icd()
    p.uc.reg_write(UC_X86_REG_FPCW, 0x027f)
    unit, mission, kind, weapon, target, offmap, controller, block, found, lead_at = \
        [HEAP + i * 0x1000 for i in range(10)]
    game = HEAP + 0x20000

    def put(a, fmt, *v):
        p.uc.mem_write(a, struct.pack('<' + fmt, *v))

    def get(a, fmt='I'):
        return struct.unpack('<' + fmt, p.uc.mem_read(a, struct.calcsize(fmt)))[0]

    put(0x62d55c, 'I', game)
    put(game + 0x19f30, 'I', offmap)
    log = {}
    answer = {}

    def record(name, value=None):
        log.setdefault(name, []).append(value)

    def allocate(uc, args):
        size = get(args)
        if size == 0x36:
            return 0, controller
        if size == 0x72:
            return 0, block
        raise AssertionError('unexpected allocation of %#x bytes' % size)

    def point_controller(uc, args):    # 4e40e0(mission, point)
        p.uc.mem_write(controller + 0x26, bytes(uc.mem_read(get(args + 4), 12)))
        put(controller + 0xa, 'h', 0)
        record('approach')
        return 2, controller

    def target_controller(uc, args):   # 4e4140(mission, target, point)
        if get(args + 4) != target:
            raise AssertionError('target controller on another unit')
        p.uc.mem_write(controller + 0x26, bytes(uc.mem_read(get(args + 8), 12)))
        put(controller + 0xa, 'h', 0)
        record('ring')
        return 3, controller

    def radius(uc, args):
        put(controller + 0xa, 'H', get(args) & 0xffff)
        return 1, 0

    def install(uc, args):
        record('install', get(args))
        put(mission + 0x6a, 'I', get(mission + 0x6a) & 0xffffc8ff)
        return 1, 0

    def lookup(uc, args):              # 4d4bf0(name): the row slot is ecx
        record('lookup', bytes(uc.mem_read(get(args), 32)).split(b'\0')[0].decode())
        return 1, uc.reg_read(UC_X86_REG_ECX)

    def construct(uc, args):           # 4d6c40(row, target, point, +0x4e, +0x52, +0x56, vec, vec)
        record('mission', (log['lookup'][-1], get(args + 4), get(args + 12, 'i'), get(args + 16, 'i'),
                           get(args + 20, 'i')))
        put(block + 4, 'B', 1 if answer['built'] else 0)
        return 12, block

    def lead(uc, args):                # 51aa50(unit, point, slot)
        p.uc.mem_write(get(args + 4), bytes(p.uc.mem_read(lead_at, 12)))
        record('lead')
        return 3, 0

    def scan(uc, args):                # 4d8370(unit)
        record('scan')
        return 1, found if answer['scan'] else 0

    def attack(uc, args):              # 4d7de0(unit, found, 0, 0, 1, 0)
        if (get(args + 4), get(args + 8), get(args + 12), get(args + 16), get(args + 20)) != (found, 0, 0, 1, 0):
            raise AssertionError('unexpected 4d7de0 arguments')
        record('attack')
        return 6, 1 if answer['attack'] else 0

    def group(uc, args):               # 51d1e0(unit, 1, 5)
        if (get(args + 4), get(args + 8)) != (1, 5):
            raise AssertionError('unexpected 51d1e0 arguments')
        record('group')
        return 3, 1 if answer['group'] else 0

    p.hooks[0x4eb9e0] = allocate
    p.hooks[0x4e40e0] = point_controller
    p.hooks[0x4e4140] = target_controller
    p.hooks[0x4e4540] = radius
    p.hooks[0x4e44c0] = lambda uc, a: (record('altitude', get(a)), (1, 0))[1]
    p.hooks[0x4d4d40] = install
    p.hooks[0x4d4bf0] = lookup
    p.hooks[0x4d6c40] = construct
    p.hooks[0x4d7750] = lambda uc, a: (record('push'), (2, 0))[1]
    p.hooks[0x4d77b0] = lambda uc, a: (record('insert'), (2, 0))[1]
    p.hooks[0x4d6da0] = lambda uc, a: (record('destroy'), (0, 0))[1]
    p.hooks[0x4eba00] = lambda uc, a: (record('free'), (0, 0))[1]
    p.hooks[0x50c480] = lambda uc, a: (record('leave'), (1, 0))[1]
    p.hooks[0x51d1e0] = group
    p.hooks[0x416c50] = lambda uc, a: (record('begin'), (4, 0))[1]
    p.hooks[0x519ac0] = lambda uc, a: (0, 0)
    p.hooks[0x519b10] = lambda uc, a: (record('auto', get(a) & 0xff), (1, 0))[1]
    p.hooks[0x519b70] = lambda uc, a: (record('manual', get(a) & 0xff), (1, 0))[1]
    p.hooks[0x51a700] = lambda uc, a: (record('aim', (get(a + 4), get(a + 8) & 0xff)), (3, 0))[1]
    p.hooks[0x51aa50] = lead
    p.hooks[0x4d8370] = scan
    p.hooks[0x4d7de0] = attack
    for address, name in [(0x51a770, 'ground aim'), (0x4e4520, 'controller heading')]:
        p.hooks[address] = (lambda n: lambda uc, a: (_ for _ in ()).throw(AssertionError('unexpected ' + n)))(name)
    p.freeze_hooks()
    draws = [0]
    p.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, d: draws.__setitem__(0, draws[0] + 1), begin=0x535cc0, end=0x535cc0)
    lost = [0]
    p.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, d: lost.__setitem__(0, lost[0] + 1), begin=0x41e28f, end=0x41e28f)

    # A hover attacker with a weapon (+0x130 0x8000000), a canfly hoverattack
    # type, slot 0 not `dropped`, D and the altitude on the type; a live
    # target on the map, not carried.
    put(unit + 0x130, 'I', 0x9000000 | 0x80000)   # Fire At Will: a lost target may push the seek
    put(unit + 0xb4, 'I', kind)
    put(unit + 0xc, 'I', weapon)
    put(kind + 0x1aa, 'I', weapon)
    put(weapon + 0xc8, 'I', 0)
    put(weapon + 0xc2, 'H', 0)
    put(weapon + 0xc4, 'h', 0)
    put(target + 0x114, 'B', 0)

    rng = random.Random(0x41d820)
    fixtures, expected = [], []
    stages = {}
    for i in range(8000):
        stage = rng.choice((0, 1, 1, 2, 3, 3, 4, 4, 4, 4, 5, 5, 6))
        events = rng.choice((1, 1, 1, 0x100, 0x400, 0x500, 0x200000, 0x400000, 0x800000, 0x10000, 0x40, 0x8, 0x20, 0x28,
                             0x200001, 0x700))
        tick = rng.randrange(1000000)
        deadline = rng.choice((0xffffffff, rng.randrange(2**32)))
        pending = rng.choice((0, 0x100, 0x500, 0x3700, rng.randrange(2**32)))
        flags = rng.choice((0x1000200, 0x1000200, 0x9000200, 0x8000000))
        count = rng.choice((0, 0, 1, 2, 3, 5, 9, 12))
        width = rng.choice((4096 - 32, 8192 - 32, rng.randrange(256, 12000)))
        height = rng.choice((4096 - 128, 8192 - 128, rng.randrange(256, 12000)))
        on_map = rng.random() < 0.85
        flies = rng.random() < 0.95
        x = rng.randrange(-300, width + 300) * 65536 + rng.randrange(65536)
        z = rng.randrange(-300, height + 300) * 65536 + rng.randrange(65536)
        y = rng.randrange(0, 512) * 65536 + rng.randrange(65536)
        spread = rng.choice((0, 16, 150, 400, 900))
        tx = x + rng.randrange(-spread, spread + 1) * 65536 + rng.randrange(65536)
        tz = z + rng.randrange(-spread, spread + 1) * 65536 + rng.randrange(65536)
        ty = rng.randrange(0, 256) * 65536
        lx = tx + rng.randrange(-40, 41) * 65536
        lz = tz + rng.randrange(-40, 41) * 65536
        ly = ty + rng.randrange(0, 60) * 65536
        point = (s32(rng.randrange(2**32)), s32(rng.randrange(2**32)), s32(rng.randrange(2**32)))
        if i % 5 == 0:
            point = (s32(lx), s32(ly), s32(lz))
        distance = rng.choice((0, 50, 100, 150, 200, 280, rng.randrange(0, 600)))
        altitude = rng.choice((0, 50, 100, 150))
        still = rng.random() < 0.6
        automatic = rng.random() < 0.5
        traffic = rng.choice((0, 2, 4, 5, 5, 7, 12, 17))
        foot = rng.choice((1, 2, 3, 4, 6))
        seed = rng.randrange(2**32)
        answer.update(group=rng.random() < 0.15, built=rng.random() < 0.95, scan=rng.random() < 0.5,
                      attack=rng.random() < 0.5)
        mover = 1 if flies or rng.random() < 0.5 else 0
        put(unit + 8, 'I', mover)
        put(kind + 0x260, 'I', 0x8000000 | (0x800 if (flies or not mover) else 0))
        flies_port = bool(mover) and bool(get(kind + 0x260) & 0x800)
        put(unit + 0x68, 'iii', s32(x), s32(y), s32(z))
        put(unit + 0x78, 'h', foot)
        put(unit + 0xa4, 'I', offmap + 4 if on_map else offmap)
        put(unit + 0x12a, 'b', traffic)
        put(kind + 0x256, 'H', distance)
        put(kind + 0x258, 'h', altitude)
        put(target + 0x68, 'iii', s32(tx), s32(ty), s32(tz))
        put(target + 8, 'I', 0 if still and rng.random() < 0.5 else 1)
        put(target + 0x130, 'I', 0x1000001 | (0 if still or not get(target + 8) else 4))
        if not still and not get(target + 8):
            put(target + 8, 'I', 1)
            put(target + 0x130, 'I', 0x1000005)
        put(lead_at, 'iii', s32(lx), s32(ly), s32(lz))
        put(game + 0x19f44, 'I', tick)
        put(game + 0x19e90, 'I', width)
        put(game + 0x19e94, 'I', height)
        put(mission + 5, 'B', stage)
        put(mission + 6, 'I', 0)
        put(mission + 0xa, 'I', deadline)
        put(mission + 0x16, 'I', target)
        put(mission + 0x22, 'iii', *point)
        put(mission + 0x4e, 'i', count)
        put(mission + 0x52, 'I', 1 if automatic else 0)
        put(mission + 0x56, 'I', 0)
        put(mission + 0x5a, 'I', flags)
        put(mission + 0x66, 'I', 0)
        put(mission + 0x6a, 'I', pending)
        put(controller + 0x26, 'iii', 0, 0, 0)
        put(controller + 0xa, 'h', 0)
        put(RNG, 'I', seed)
        log.clear()
        draws[0] = lost[0] = 0
        result, error = p.call(0x41d820, (unit, mission, events))
        if error:
            raise AssertionError((i, error))
        if log.get('auto') and any(a != 3 for a in log['auto']):
            raise AssertionError((i, 'auto slots', log['auto']))
        if log.get('manual') and any(a != 0 for a in log['manual']):
            raise AssertionError((i, 'manual slot', log['manual']))
        if log.get('aim') and any(a != (target, 0) for a in log['aim']):
            raise AssertionError((i, 'aim', log['aim']))
        missions = log.get('mission', [])
        steps = [m for m in missions if m[0] == 'VTOL_StepOut']
        formations = [m for m in missions if m[0] == 'VTOL_Move_Formation']
        for m in formations:
            # target 0, +0x4e 0, +0x52 0 (the hover's +0x52 is below 100), +0x56 -4
            if m[1:] != (0, 0, 0, -4):
                raise AssertionError((i, 'formation arguments', m))
        controllers = len(log.get('approach', [])) + len(log.get('ring', []))
        goal = struct.unpack('<3i', p.uc.mem_read(controller + 0x26, 12)) if controllers else (0, 0, 0)
        built = answer['built']
        expected.append((result, get(mission + 5, 'B'), get(mission + 6), get(mission + 0xa), get(mission + 0x6a),
                         get(mission + 0x5a), get(mission + 0x4e, 'i'),
                         *struct.unpack('<3i', p.uc.mem_read(mission + 0x22, 12)),
                         len(log.get('approach', [])), len(log.get('ring', [])), *goal,
                         get(controller + 0xa, 'h') if controllers else 0,
                         lost[0], len(log.get('group', [])), len(log.get('leave', [])), len(formations),
                         len(log.get('begin', [])), len(log.get('auto', [])), len(log.get('manual', [])),
                         len(log.get('lead', [])), len(log.get('scan', [])), len(steps),
                         steps[-1][2] if steps else 0, get(RNG), draws[0]))
        # The port's retarget() is the scan and the attack together: it answers
        # yes only when both do.
        retargets = int(answer['scan'] and answer['attack'])
        fixtures.append(' '.join(map(str, (stage, events, tick, deadline, pending, flags, count, s32(x), s32(y), s32(z),
                                           s32(tx), s32(ty), s32(tz), s32(lx), s32(ly), s32(lz), *point, distance,
                                           int(flies_port), 1, int(still), int(automatic), traffic, foot,
                                           int(on_map), width, height, seed, int(answer['group']), int(built),
                                           retargets, int(built), 5))))
        key = 'off' if not on_map else stage
        stages[key] = stages.get(key, 0) + 1
    rows = run(binary, '--air-hover', fixtures)
    # The port's row: result stage waitMask deadline pending flags count point(3) approaches rings goal(3) radius
    # flags heading lost out leaves formations begin auto aims targetPoints leads retargets stepOuts stepOutD seed calls.
    actual = []
    for r in rows:
        (result, stage, wait, deadline, pending, flags, count, px, py, pz, approaches, rings, gx, gy, gz, radius,
         gflags, gheading, lostc, outc, leaves, formations, begin, autos, aims, targets, leads, retargets, steps,
         stepd, seed, calls) = r
        actual.append((result, stage, wait, deadline, pending, flags, count, px, py, pz, approaches, rings, gx, gy, gz,
                       radius, lostc, outc, leaves, formations, begin, autos, aims, leads, retargets, steps, stepd,
                       seed, calls))
    compare('hover', fixtures, expected, actual)
    def tally(column):
        return sum(1 for row in expected if row[column])
    print('PASS: AirToGroundHover, %d calls (%s), every state, point, controller, slot call, push and draw '
          '(%d approaches, %d ring points, %d lost, %d leaves, %d formations, %d rescans, %d step-outs, '
          '%d ended (8))'
          % (len(fixtures), ' '.join('%s:%d' % kv for kv in sorted(stages.items(), key=str)), tally(10), tally(11),
             tally(16), tally(18), tally(19), tally(24), tally(25), sum(1 for row in expected if row[0] == 8)))


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else 'build-dbg/retail_motion_test'
    check_counter(binary)
    check_step_out(binary)
    check_hover(binary)


if __name__ == '__main__':
    main()
