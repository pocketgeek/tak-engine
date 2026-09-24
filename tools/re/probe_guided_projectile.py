#!/usr/bin/env python3
"""Compare native GuidedWeapon launch/XYZ steering with the deterministic port.

The source muzzle, current target SweetSpot and per-substep collision result are
controlled sinks. Native 0x52c540, 0x52e030 and 0x52c6d0 remain executable;
the probe compares their fixed-point XYZ/BAM records with retail_visual_test's
guided-projectile mode. No retail GUI is launched.
"""
import random
import struct
import subprocess
import sys

from emu import HEAP, Icd
from unicorn.x86_const import UC_X86_REG_FPCW


def put(uc, address, value):
    uc.mem_write(address, struct.pack('<I', value & 0xffffffff))


def putf(uc, address, value):
    uc.mem_write(address, struct.pack('<f', value))


def signed(value):
    return struct.unpack('<i', struct.pack('<I', value & 0xffffffff))[0]


def i32s(uc, address, count=3):
    return struct.unpack('<' + 'i' * count, uc.mem_read(address, 4 * count))


def u16s(uc, address, count=3):
    return struct.unpack('<' + 'H' * count, uc.mem_read(address, 2 * count))


def ptr(uc, address):
    return struct.unpack('<I', uc.mem_read(address, 4))[0]


p = Icd()
p.uc.reg_write(UC_X86_REG_FPCW, 0x027f)  # Native converter temporarily truncates float-to-int.
game, data, guided, shot, owner, target, aim, unit_type, art, script = [
    HEAP + i * 0x30000 for i in range(1, 11)]
origin = (0, 0, 0)
target_point = (0, 0, 0)
slot = 0
collision_at = 0
collision_calls = 0
impact_events = 0
collision_rows = []


def muzzle(uc, sp):
    args = struct.unpack('<4I', uc.mem_read(sp, 16))
    assert args == (owner, shot + 4, slot, 0xffffffff), args
    uc.mem_write(args[1], struct.pack('<3i', *origin))
    return 4, 0


def veteran(uc, sp):
    assert ptr(uc, sp) == owner
    return 1, 0


def buildup(uc, sp):
    return 3, 0


def sweetspot(uc, sp):
    args = struct.unpack('<2I', uc.mem_read(sp, 8))
    assert args[0] == target and args[1] != 0, args
    uc.mem_write(args[1], struct.pack('<3i', *target_point))
    return 2, 0


def collision(uc, sp):
    global collision_calls
    assert ptr(uc, sp) == shot
    collision_calls += 1
    collision_rows.append((i32s(uc, shot + 4), i32s(uc, shot + 0x1c), u16s(uc, shot + 0x34)))
    return 1, 2 if collision_at and collision_calls == collision_at else 0


def impact(uc, sp):
    global impact_events
    values = struct.unpack('<5I', uc.mem_read(sp, 20))
    assert values == (shot, 123, 0, 1, 0), values
    impact_events += 1
    return 5, 0


p.hooks.update({0x4dd420: muzzle, 0x4dd4f0: sweetspot, 0x519310: veteran,
                0x537390: buildup, 0x52a4d0: collision, 0x529c10: impact})
p.freeze_hooks()
put(p.uc, 0x62d55c, game)
put(p.uc, game + 0x19f44, 100)
put(p.uc, owner + 0xb4, unit_type)
put(p.uc, owner + 0xc0, script)
put(p.uc, owner + 0x130, 0x01000000)
put(p.uc, target + 0x130, 0x01000000)
put(p.uc, unit_type + 0x8a, art)
put(p.uc, aim, data)
put(p.uc, shot + 0x80, 123)
put(p.uc, shot + 0x7c, owner)
put(p.uc, shot + 0x78, target)
put(p.uc, guided, 0x5f37e4)

rng = random.Random(0x52e03052c6d0)
rows = []
expected = []
cases = 384
binary = sys.argv[1] if len(sys.argv) > 1 else 'build/retail_visual_test'
for case in range(cases):
    # Include flat ground, flyers with positive/negative vertical separation,
    # straight/vertical headings, moving SweetSpots, and exact/near turn caps.
    origin = tuple(rng.randrange(-3000, 3000) * 65536 for _ in range(3))
    body = rng.randrange(65536)
    heading = rng.choice((0, 0x4000, 0x8000, 0xc000, rng.randrange(65536)))
    pitch = 0 if case % 9 == 0 else rng.choice((0, 0x1000, 0x4000, 0xc000, rng.randrange(65536)))
    speed = rng.choice((0x100000, 0x400000, 0x800000, rng.randrange(0x20000, 0x900000)))
    steps = rng.choice((1, 2, 3, 4, 7))
    turn = rng.choice((0.0, 0.002, 0.05, 0.2, 1.2, rng.random() * 0.45))
    if case in (1, 2):
        # Current velocity points +Z and the target lies exactly on -Z.
        # The cross axis is zero while dot is negative (the antiparallel edge).
        body, heading, pitch, turn = 0, 0, 0, 0.2
    update_angles = case % 2 == 0
    if case in (1, 2):
        # Keep the native zero-axis antiparallel case with refresh disabled;
        # refreshing its BAM direction takes a native degenerate quaternion
        # path (0x2023bc) and faults before the base movement routine.
        update_angles = case == 2
    putf(p.uc, guided + 4, turn)
    put(p.uc, data + 0xcc, speed)
    put(p.uc, data + 0xd0, steps)
    spin = tuple(rng.randrange(-8, 9) & 0xffff for _ in range(3))
    for offset, value in zip((0xb8, 0xba, 0xbc), spin):
        p.uc.mem_write(data + offset, struct.pack('<H', value))
    put(p.uc, data + 0x48, 111 if update_angles else 0)  # +93 gates yaw/pitch refresh
    put(p.uc, data + 0x4c, 0)
    put(p.uc, data + 0x50, 0)
    put(p.uc, data + 0x54, 0)
    put(p.uc, data + 0x58, 0)
    put(p.uc, data + 0x90, 5000)
    put(p.uc, data + 0xc8, 0)
    put(p.uc, owner + 0x7e, body)
    put(p.uc, aim + 0x16, heading)
    put(p.uc, aim + 0x18, pitch)
    put(p.uc, aim + 0x1a, slot)

    # A stable nominal shape plus vertical and lateral offsets; targets then
    # move independently between updates, as queryUnitScriptPoint does in World.
    dx = rng.choice((0, 300, 900, 2400, 6000))
    dy = rng.choice((0, -2400, -600, 600, 2400))
    dz = rng.choice((0, 300, 900, 2400, 6000))
    if dx == dz == 0 and dy == 0:
        dz = 900
    target_point = tuple(origin[i] + (dx, dy, dz)[i] for i in range(3))
    p.uc.mem_write(shot, bytes(0xb0))
    put(p.uc, shot, data)
    put(p.uc, shot + 0x78, target)
    put(p.uc, shot + 0x7c, owner)
    put(p.uc, shot + 0x80, 123)
    origin_saved = origin
    _, error = p.call(0x52c540, (shot, owner, aim), ecx=guided)
    assert error is None, (case, error)
    # The common initializer's wrapped range lifetime depends on its weapon-aim
    # wrapper. Give the guided update a controlled live window after validating
    # the actual launch XYZ/velocity/angles above.
    put(p.uc, shot + 0x60, 100)
    put(p.uc, shot + 0x70, 10000)
    initial = (i32s(p.uc, shot + 4), i32s(p.uc, shot + 0x1c), u16s(p.uc, shot + 0x34))
    assert initial[0] == origin_saved, (case, initial, origin_saved)
    rows.append(f'L {origin_saved[0]} {origin_saved[1]} {origin_saved[2]} {body} {heading} {pitch} {speed}')
    expected.append((initial, None, None))

    # Two updates exercise moving/vertical tracking. A fraction hit exercises
    # base collision's stop-after-code-2 rule. Code-1 continuation is not
    # exercised by this native harness, whose callback returns only 0 or 2.
    py_state = initial
    for update in range(2):
        delta = (rng.randrange(-900, 6001), rng.choice((0, -1800, -300, 300, 1800)),
                 rng.randrange(-900, 6001))
        if case % 9 == 0 and update == 0:
            delta = (0, rng.choice((-3600, -1200, 1200, 3600)), 0)  # vertical aim
        if case == 1 and update == 0:
            delta = (0, 0, -900)  # exact antiparallel to the +Z launch vector
        elif case == 2 and update == 0:
            delta = (300, 0, -900)  # finite cross axis near antiparallel, refresh enabled
        target_point = tuple(origin_saved[i] + delta[i] * 65536 for i in range(3))
        collision_calls = impact_events = 0
        collision_rows.clear()
        collision_at = (1 if case % 34 == 0 else max(1, steps // 2)) if case % 17 == 0 and update == 1 else 0
        put(p.uc, game + 0x19f44, 102 + update)
        _, error = p.call(0x52e030, (shot,), ecx=guided)
        assert error is None, (case, update, error, origin_saved, target_point, turn, speed, steps,
            body,heading,pitch,initial,i32s(p.uc,shot+4),i32s(p.uc,shot+0x1c),u16s(p.uc,shot+0x34),
            collision_rows[-1:] if collision_rows else [])
        native = (i32s(p.uc, shot + 4), i32s(p.uc, shot + 0x1c), u16s(p.uc, shot + 0x34))
        rows.append('T ' + ' '.join(map(str, (*py_state[0], *py_state[1], *py_state[2],
            *target_point, format(turn, '.9g'), speed, steps, *spin, int(update_angles),
            collision_at))))
        expected.append((native, collision_calls, bool(collision_at)))
        if collision_at:
            assert collision_calls == collision_at and impact_events == 1, (case, collision_calls, impact_events)
            assert p.uc.mem_read(shot + 0x97, 1)[0] == 1, case
        else:
            assert collision_calls == steps and impact_events == 0, (case, update, collision_calls, impact_events,
                ptr(p.uc,shot+0x78), ptr(p.uc,shot+0x7c), ptr(p.uc,shot+0x60), ptr(p.uc,shot+0x70),
                struct.unpack('<I',p.uc.mem_read(game+0x19f44,4))[0],struct.unpack('<I',p.uc.mem_read(owner+0x130,4))[0])
        py_state = native
        if collision_at:
            break
        # Current position is the source for next update; native receives the
        # target point explicitly, and only the target continues moving.

ported = subprocess.run([binary, '--guided-projectile'], input='\n'.join(rows) + '\n',
                        text=True, capture_output=True, check=True)
ported_rows = [tuple(map(int, line.split())) for line in ported.stdout.splitlines()]
assert len(ported_rows) == len(expected), (len(ported_rows), len(expected), ported.stderr)
worst = [0] * 9
for index, (actual, (native, collisions, hit)) in enumerate(zip(ported_rows, expected)):
    want = (*native[0], *native[1], *native[2])
    if collisions is None:
        assert len(actual) == 9, (index, actual)
    else:
        assert len(actual) == 11, (index, actual)
        assert actual[9:] == (collisions, int(hit)), (index, actual, collisions, hit)
    for field in range(9):
        if field < 6:
            delta = abs(signed(actual[field] - want[field]))
            worst[field] = max(worst[field], delta)
            assert delta <= (128 if field < 6 else 1), (index, field, actual, want, delta, rows[index])
        else:
            delta = ((actual[field] - want[field] + 0x8000) & 0xffff) - 0x8000
            worst[field] = max(worst[field], abs(delta))
            assert abs(delta) <= 3, (index, field, actual, want, delta)
print(f'PASS: {cases} native guided launches plus {len(expected)-cases} moving/vertical XYZ updates; '
      f'position/velocity max raw deltas={worst[:6]}, BAM deltas={worst[6:]}; '
      'native collision callback admitted every substep and stopped correctly on code 2')
