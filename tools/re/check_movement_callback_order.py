#!/usr/bin/env python3
"""Compare movement callback order in retail 0x4dc800 with the client helper.

The fixture follows the native point-flight setup used by
check_air_flight_motion_trace.py. It records the real retail COB call-ins on a
tick where turn direction, movement tier and flight occupancy all change, then
checks that a no-change tick is silent. No retail GUI is launched.
"""
import argparse
import struct
import subprocess

from emuphase import ARENA, GS, TYPE, Phase
from unicorn.x86_const import UC_X86_REG_FPCW


def put(uc, address, *values):
    uc.mem_write(address, struct.pack('<' + 'I' * len(values),
                                      *(value & 0xffffffff for value in values)))


def native_trace():
    p = Phase(64, 64)
    p.uc.reg_write(UC_X86_REG_FPCW, 0x027f)
    unit = p.unit(10, 10)
    mover, navigator = unit + 0x300, ARENA + 0x0d10000
    settings, options = ARENA + 0x0d21000, ARENA + 0x0d20000

    put(p.uc, 0x62d55c, GS)
    put(p.uc, 0x62d558, settings)
    put(p.uc, settings + 8, options)
    p.uc.mem_write(options, bytes(0x100))
    sector_stride = 8
    sectors = p._alloc(sector_stride * sector_stride * 10)
    put(p.uc, GS + 0x19f18, sectors)
    put(p.uc, GS + 0x19f1c, sector_stride)
    put(p.uc, GS + 0x19f30, 1)
    for index in range(sector_stride * sector_stride):
        p.uc.mem_write(sectors + index * 10 + 1, b'\x64')
    current_sector = sectors + ((10 * 16 >> 7) * sector_stride + (10 * 16 >> 7)) * 10

    start, target = (160, 100, 160), (800, 100, 800)
    put(p.uc, unit + 0xa4, current_sector)
    put(p.uc, current_sector + 6, unit)
    put(p.uc, unit + 0x68, *(value << 16 for value in start))
    p.uc.mem_write(unit + 0x7e, struct.pack('<H', 0))
    put(p.uc, unit + 0x12b, 120000)
    put(p.uc, unit + 0x130, 0x01000000)
    put(p.uc, mover, navigator)
    put(p.uc, mover + 0x20, 0)
    put(p.uc, mover + 0x30, 0x7fffffff)
    p.uc.mem_write(mover + 0x36, struct.pack('<H', 2))

    put(p.uc, navigator, 0x5f34d4)
    put(p.uc, navigator + 8, unit)
    put(p.uc, navigator + 0x0c, *(value << 16 for value in start))
    p.uc.mem_write(navigator + 0x24, struct.pack('<H', 0))

    # Match the checked native flight fixture: an enabled flyer on flat terrain.
    p.uc.mem_write(TYPE + 0x166, struct.pack('<i', 60000))
    p.uc.mem_write(TYPE + 0x16a, struct.pack('<i', 20000))
    p.uc.mem_write(TYPE + 0x16e, struct.pack('<i', 65536))
    p.uc.mem_write(TYPE + 0x172, struct.pack('<i', 65536))
    p.uc.mem_write(TYPE + 0x182, struct.pack('<i', 2 * 65536))
    p.uc.mem_write(TYPE + 0x186, struct.pack('<i', 4 * 65536))
    p.uc.mem_write(TYPE + 0x18e, struct.pack('<H', 10000))
    p.uc.mem_write(TYPE + 0x23a, struct.pack('<h', 100))
    put(p.uc, TYPE + 0x260, 0x800)

    controller, mission, point = (p._alloc(n) for n in (0x100, 0x100, 12))
    put(p.uc, mission + 0x0e, unit)
    put(p.uc, point, *(value << 16 for value in target))
    _, error = p.icd.call(0x4e40e0, (mission, point), ecx=controller)
    assert error is None, error
    _, error = p.icd.call(0x4e4540, (16,), ecx=controller)
    assert error is None, error
    put(p.uc, navigator + 4, controller)

    calls = []

    def capture(uc, sp):
        args = struct.unpack('<8I', uc.mem_read(sp, 32))
        name = bytes(uc.mem_read(args[0], 64)).split(b'\0')[0].decode('ascii')
        values = tuple(struct.unpack('<i', struct.pack('<I', value))[0]
                       for value in args[4:4 + args[3]])
        calls.append((name, values))
        return 8, 0

    p.icd.hooks[0x56c640] = capture
    p.icd.freeze_hooks()

    traces = []
    for tick in (1, 2):
        put(p.uc, GS + 0x19f44, tick)
        calls.clear()
        _, error = p.icd.call(0x4dc800, (unit,), ecx=mover)
        assert error is None, (tick, error)
        traces.append(list(calls))
    return traces


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build-o2/retail_movement_animation_test')
    args = parser.parse_args()

    retail = native_trace()
    expected = [[('TurnDirection', (-135,)), ('MoveRate', (1,)),
                 ('setSFXoccupy', (5,))], []]
    if retail != expected:
        raise AssertionError(f'retail 0x4dc800 callback trace differed: {retail!r}')

    rows = '-135 1 5\n-135 1 5\n'
    result = subprocess.run([args.binary, '--trace'], input=rows,
                            check=True, capture_output=True, text=True)
    client = result.stdout.splitlines()
    expected_client = ['TurnDirection(-135) MoveRate(1) setSFXoccupy(5)', '-']
    if client != expected_client:
        raise AssertionError(f'client callback trace differed: {client!r}')
    print('PASS: native retail 0x4dc800 and shared client helper dispatch '
          'TurnDirection(-135), MoveRate(1), setSFXoccupy(5), then stay silent '
          'on an unchanged tick')


if __name__ == '__main__':
    main()
