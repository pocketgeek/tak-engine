#!/usr/bin/env python3
"""Pair retail's placed-Zhon construction orbit with World flight movement.

One emulator instance executes the actual 41ef00 stage-5 -> stage-4 handoff,
the point-controller constructor/radius setter/navigator binder, and then the
real 4dc800 mover (including navigator vfunc 524af0) on a flat 100-height
plane. World receives that exact start state, site, and installed orbit point,
using the asset-backed zonhunt profile. No retail GUI is launched.
"""
import argparse
import struct
import subprocess

from emu import Icd, HEAP


def put(uc, address, *values):
    uc.mem_write(address, struct.pack('<' + 'I' * len(values),
                                      *(value & 0xffffffff for value in values)))


def signed_words(uc, address, count):
    return struct.unpack('<' + 'i' * count, uc.mem_read(address, count * 4))


def native_trace(steps, seed):
    p = Icd()
    uc = p.uc
    game, unit, site, kind, owner, mission = (HEAP + n * 0x10000
                                               for n in range(1, 7))
    mover = unit + 0x300
    navigator = HEAP + 0x80000
    settings, options, sectors = (HEAP + n * 0x10000 for n in range(9, 12))
    # Match the reported retail body height at this stage of the work cycle,
    # then let the actual mover converge toward the shared flat-plane cruise Y.
    start = (1000, 161, 1000)
    site_position = (1120, 100, 1060)
    terrain = 100

    put(uc, 0x62d55c, game)
    put(uc, 0x62d558, settings)
    put(uc, settings + 8, options)
    uc.mem_write(options, bytes(0x100))
    put(uc, 0x62db84, game + 0x30000)
    put(uc, 0x65e108, 0)
    uc.mem_write(0x65e01c, b'\x01')
    uc.mem_write(0x65e02c, b'\x00')
    uc.mem_write(0x65e020, b'\x01')
    uc.mem_write(0x65e034, b'\x00')
    put(uc, 0x629680, 0)
    put(uc, 0x66e3d8, 1)
    put(uc, game + 0x19e90, 4096, 4096)
    put(uc, game + 0x19e98, 128, 128)
    put(uc, game + 0x19f30, 1000)
    put(uc, game + 0x19f44, 1000)
    put(uc, game + 0x175c4 + 0x126, kind)
    put(uc, 0x64186c, seed)

    # The native sector table stores one height byte per 128-pixel sector.
    sector_stride = 32
    put(uc, game + 0x19f18, sectors)
    put(uc, game + 0x19f1c, sector_stride)
    sector_data = bytearray(sector_stride * sector_stride * 10)
    sector_data[1::10] = bytes([terrain]) * (sector_stride * sector_stride)
    uc.mem_write(sectors, bytes(sector_data))
    sector = sectors + ((start[2] >> 7) * sector_stride + (start[0] >> 7)) * 10
    put(uc, unit + 0xa4, sector)
    put(uc, sector + 6, unit)

    put(uc, owner, 1)
    uc.mem_write(owner + 0xea, b'\x00')
    put(uc, unit + 0x08, mover)
    put(uc, unit + 0x60, mission)
    put(uc, unit + 0x68, *(value << 16 for value in start))
    uc.mem_write(unit + 0x74, struct.pack('<4h', start[0] // 16,
                 start[2] // 16, 2, 2))
    uc.mem_write(unit + 0x7e, struct.pack('<H', 0))
    put(uc, unit + 0xb4, kind)
    put(uc, unit + 0xb8, owner)
    put(uc, unit + 0x12b, 163840)     # 2.5 maxvelocity, exact, no speed spread
    put(uc, unit + 0x130, 0x01000000)

    # FBI values from zonhunt.fbi, stored in retail's native fixed-point fields.
    put(uc, kind + 0x162, 163840)
    put(uc, kind + 0x166, 13107)      # brakerate 0.2
    put(uc, kind + 0x16a, 32768)      # acceleration 0.5
    put(uc, kind + 0x16e, 65536)      # watermultiplier default 1.0
    put(uc, kind + 0x172, 0x13333)    # retail roadmultiplier default 1.2
    uc.mem_write(kind + 0x18e, struct.pack('<H', 400))
    put(uc, kind + 0x230, 100)        # builddistance
    uc.mem_write(kind + 0x23a, struct.pack('<h', 150))  # cruisealt
    put(uc, kind + 0x260, 0x800)      # canfly
    uc.mem_write(kind + 0x126, struct.pack('<HH', 2, 2))

    put(uc, mover, navigator)
    put(uc, mover + 0x20, 0)
    put(uc, mover + 0x30, 0x7fffffff)  # keep sector terrain scan fixed for trace
    uc.mem_write(mover + 0x36, struct.pack('<H', 2))
    put(uc, navigator, 0x5f34d4)
    put(uc, navigator + 8, unit)
    put(uc, navigator + 0x0c, *(value << 16 for value in start))
    uc.mem_write(navigator + 0x24, struct.pack('<H', 0))

    put(uc, site + 0x68, *(value << 16 for value in site_position))
    put(uc, site + 0xb4, kind)
    put(uc, site + 0x130, 0x01000000)
    put(uc, site + 0x108, 0x3f000000)  # incomplete construction site
    put(uc, mission + 0x0e, unit)
    put(uc, mission + 0x16, site)
    put(uc, mission + 4, 1)
    uc.mem_write(mission + 5, b'\x05')
    put(uc, mission + 6, 1)
    put(uc, mission + 10, 0xffffffff)
    put(uc, unit + 0x60, mission)

    # OS-only allocation/critical-section edges are the same substitutions as
    # probe_flying_construction_orbit.py. The handler, goal controller, setter,
    # navigator and mover all remain native retail code.
    stub_cs = HEAP + 0x3f0000
    stub_heap_alloc = HEAP + 0x3f0010
    heap_cursor = [HEAP + 0x3f1000]
    put(uc, 0x5eb118, stub_cs)
    put(uc, 0x5eb108, stub_cs)
    put(uc, 0x5eb110, stub_cs)
    put(uc, 0x5eb268, stub_heap_alloc)

    def one_arg_service(_uc, _sp):
        return 1, 0

    def heap_alloc(_uc, sp):
        requested = struct.unpack('<I', uc.mem_read(sp + 8, 4))[0]
        ptr = heap_cursor[0]
        size = (requested + 15) & ~15
        heap_cursor[0] += max(size, 16)
        uc.mem_write(ptr, bytes(size))
        return 3, ptr

    p.hooks.update({stub_cs: one_arg_service,
                    stub_heap_alloc: heap_alloc,
                    0x5d3d12: lambda _uc, _sp: (0, 0),
                    0x56c640: lambda _uc, _args: (8, 0)})
    p.freeze_hooks()

    result, error = p.call(0x41ef00, args=(unit, mission, 1))
    if error:
        raise RuntimeError(('41ef00 stage 5', error))
    if result != 4:
        raise AssertionError(f'41ef00 did not continue to orbit stage 4: {result}')
    result, error = p.call(0x41ef00, args=(unit, mission, 0))
    if error:
        raise RuntimeError(('41ef00 stage 4', error))
    active = struct.unpack('<I', uc.mem_read(navigator + 4, 4))[0]
    if not active:
        raise AssertionError('41ef00 did not install its controller on navigator')
    goal = signed_words(uc, active + 0x26, 3)
    flags, _, _, heading = struct.unpack('<4H', uc.mem_read(active + 8, 8))
    if flags != 0x60:
        raise AssertionError(f'expected construction controller flags 0x60, got {flags:#x}')
    if seed == 50 and (goal != (68223960, 6553600, 66009400) or heading != 43016):
        raise AssertionError({'seed-50 native orbit goal': goal, 'heading': heading})

    rows = []
    for tick in range(1, steps + 1):
        put(uc, game + 0x19f44, tick)
        put(uc, unit + 0xa4, sector)
        _, error = p.call(0x4dc800, args=(unit,), ecx=mover)
        if error:
            raise RuntimeError({'tick': tick, '4dc800': error})
        position = signed_words(uc, unit + 0x68, 3)
        body_heading = struct.unpack('<H', uc.mem_read(unit + 0x7e, 2))[0]
        speed = struct.unpack('<i', uc.mem_read(mover + 0x20, 4))[0]
        velocity = signed_words(uc, mover + 8, 3)
        nav_out = signed_words(uc, navigator + 0x0c, 6)
        nav_heading = struct.unpack('<H', uc.mem_read(navigator + 0x24, 2))[0]
        rows.append((tick, *position, body_heading, speed, *velocity,
                     *nav_out, nav_heading,
                     position[0] - (site_position[0] << 16),
                     position[2] - (site_position[2] << 16)))
    metadata = {'goal': goal, 'goal_heading': heading, 'site': site_position,
                'start': start, 'terrain': terrain, 'max_speed': 163840}
    return metadata, rows


def world_trace(binary, install, steps, metadata):
    goal = metadata['goal']
    site = metadata['site']
    start = metadata['start']
    args = [binary, '--construction-flight-trace', install, str(steps),
            str(metadata['terrain']), *(str(value << 16) for value in start),
            '0', str(goal[0]), str(goal[2]), str(metadata['goal_heading']),
            str(site[0] << 16), str(site[2] << 16), str(metadata['max_speed'])]
    result = subprocess.run(args, check=True, capture_output=True, text=True)
    lines = result.stdout.splitlines()
    profile = tuple(map(int, lines[0].split()[1:]))
    rows = [tuple(map(int, line.split())) for line in lines[1:]]
    if len(rows) != steps:
        raise AssertionError(f'World emitted {len(rows)} rows, expected {steps}')
    return profile, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/conjure_test')
    parser.add_argument('--install', default='assets/game')
    parser.add_argument('--steps', type=int, default=16)
    parser.add_argument('--seed', type=lambda s: int(s, 0), default=50)
    args = parser.parse_args()
    steps = max(1, min(args.steps, 10000))
    metadata, retail = native_trace(steps, args.seed)
    profile, world = world_trace(args.binary, args.install, steps, metadata)
    expected_profile = (163840, 32768, 13107, 0x13333, 400, 150, 100)
    if profile != expected_profile:
        raise AssertionError({'World zonhunt profile': profile,
                              'expected asset profile': expected_profile})
    for tick, (native_row, world_row) in enumerate(zip(retail, world), 1):
        if native_row != world_row:
            raise AssertionError({'tick': tick, 'retail': native_row,
                                  'World': world_row})
    if steps >= 16 and (retail[0][2] != 162 * 65536 or
                        retail[15][2] != 177 * 65536):
        raise AssertionError('expected the 161-to-177 world-unit altitude climb')
    print(f"PASS: {steps} exact native/World Zhon placed-construction flight ticks; "
          f"native 41ef00 goal {metadata['goal']} (heading {metadata['goal_heading']}) "
          "flows through the installed native controller and 4dc800/524af0; "
          "XYZ, Y altitude, heading, velocity, navigator output, and site-relative X/Z match")


if __name__ == '__main__':
    main()
