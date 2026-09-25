#!/usr/bin/env python3
"""Exercise retail 0x529c10 impact dispatch without launching the game.

Only engine-facing services are substituted: map/cell lookup, weapon damage
lookup, final unit/feature mutation, effect queue insertion, and projectile
COM releases. Retail impact, splash scan, per-hit damage math, feature scan,
and projectile retirement run from KINGDOMS.icd.
"""
import pathlib
import re
import struct

from emu import HEAP, Icd
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_FPCW


ROOT = pathlib.Path(__file__).resolve().parents[2]
GRID = 64
STRIDE = 0x138


def put(uc, address, value):
    uc.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))


def put16(uc, address, value):
    uc.mem_write(address, struct.pack("<H", value & 0xFFFF))


def putf(uc, address, value):
    uc.mem_write(address, struct.pack("<f", value))


def read32(uc, address):
    return struct.unpack("<I", uc.mem_read(address, 4))[0]


def shipped_profile(unit, weapon, expected):
    text = (ROOT / f"assets/extracted/all/units/{unit}.fbi").read_text(errors="replace")
    match = re.search(rf"\[WEAPON{weapon}\]\s*\{{(.*?)\n\}}", text, re.I | re.S)
    assert match, f"missing {unit} WEAPON{weapon}"
    block = match.group(1)
    damage = re.search(r"\[DAMAGE\]\s*\{(.*?)\n\s*\}", block, re.I | re.S)
    assert damage, f"missing {unit} WEAPON{weapon} damage table"
    for key, value in expected.items():
        haystack = damage.group(1) if key == "default" else block
        assert re.search(rf"\b{key}\s*=\s*{re.escape(str(value))}\s*;", haystack, re.I), \
            f"{unit} WEAPON{weapon} {key} did not match {value}"
    return block


def make_probe(base_damage, aoe, edge, effect=False, units=()):
    p = Icd()
    uc = p.uc
    names = ("game", "shot", "weapon", "owner", "units", "kind", "app",
             "appcore", "cells", "features", "rules", "vtable", "iface",
             "result", "featurepos", "featuremap")
    addr = {name: HEAP + i * 0x10000 for i, name in enumerate(names)}
    G, S, W, O, U, K, A, AC, C, F, R, VT, IF, RES, FP, FM = (addr[x] for x in names)
    logs = []

    def cell_at(x, z):
        return C + (z * GRID + x) * 14

    def map_cell(emu, sp):
        point = read32(emu, sp)
        x, y, z = struct.unpack("<3i", emu.mem_read(point, 12))
        logs.append(("map", x // 65536, z // 65536))
        return 1, cell_at((x // 65536) // 16, (z // 65536) // 16)

    def map_index(emu, sp):
        x, z = struct.unpack("<ii", emu.mem_read(sp, 8))
        return 2, cell_at(x, z)

    def feature_point(emu, sp):
        emu.mem_write(FP, struct.pack("<3i", 256 * 65536, 10 * 65536, 256 * 65536))
        return 3, FP

    def feature_damage(emu, sp):
        logs.append(("feature-dispatch", struct.unpack("<5I", emu.mem_read(sp, 20))))
        return 5, 0

    def damage_lookup(emu, sp):
        logs.append(("damage-lookup", read32(emu, sp)))
        return 1, base_damage

    def apply_damage(emu, sp):
        args = struct.unpack("<5I", emu.mem_read(sp, 20))
        logs.append(("unit-dispatch", args))
        return 5, 0

    def effect_check(emu, sp):
        logs.append(("effect-check", struct.unpack("<3i", emu.mem_read(read32(emu, sp), 12))))
        return 1, 1

    def effect_dispatch(emu, sp):
        logs.append(("effect-create", struct.unpack("<2I", emu.mem_read(sp, 8))))
        return 2, 0

    def environment_event(emu, sp):
        logs.append(("environment-event", struct.unpack("<3I", emu.mem_read(sp, 12))))
        return 3, 0

    def reference_release(emu, sp):
        logs.append(("release", emu.reg_read(UC_X86_REG_ECX), read32(emu, sp)))
        return 1, 0

    hooks = {
        0x50E660: map_cell, 0x50E600: map_index,
        0x531E50: damage_lookup, 0x51A140: apply_damage,
        0x50A7D0: environment_event, 0x4931E0: feature_point,
        0x4961A0: feature_damage, 0x48C870: effect_check,
        0x48C0B0: effect_dispatch, 0x5D4444: lambda emu, sp: (0, 0),
        0x51F340: lambda emu, sp: (2, 1), 0x4099E0: lambda emu, sp: (3, 0),
        VT + 0x14: lambda emu, sp: (1, 0),
    }
    dtor = HEAP + 0xFE000
    hooks[dtor] = reference_release
    p.hooks.update(hooks)
    p.freeze_hooks()
    uc.reg_write(UC_X86_REG_FPCW, 0x027F)

    put(uc, 0x62D55C, G)
    put(uc, 0x62D558, A)
    put(uc, A, AC)
    put(uc, G + 0x175DC, R)
    put(uc, G + 0x19E98, GRID)
    put(uc, G + 0x19E9C, GRID)
    put(uc, G + 0x19EC0, 10)
    put(uc, G + 0x19EDC, F)
    put(uc, G + 0x19ECC, 0)
    put(uc, G + 0x19F04, C)
    put(uc, G + 0x19E74, FM)
    put(uc, G + 0x14E84, U)
    put(uc, G + 0x14E88, U + 6 * STRIDE)
    put(uc, G + 0x14ED0, 0)
    put(uc, G + 0x14ED4, 0)
    put(uc, G + 0x19E54, 10000)
    put(uc, G + 0x19E58, 10000)
    put(uc, 0x60596C, 1)
    uc.mem_write(G + 0x19EF8, b"\xff")

    for z in range(GRID):
        for x in range(GRID):
            c = cell_at(x, z)
            uc.mem_write(c, bytes(14))
            uc.mem_write(c + 5, b"\x0a\x0a")
            put16(uc, c + 8, 0xFFFF)
    for unit_id, (x, z, player) in enumerate(units, start=1):
        target = U + unit_id * STRIDE
        put(uc, target + 0xB4, K)
        put(uc, target + 0x130, 0x01000000)
        uc.mem_write(target + 0xFD, bytes((player,)))
        putf(uc, target + 0xE4, 1.0)
        put(uc, target + 0x68, x * 65536)
        put(uc, target + 0x6C, 0)
        put(uc, target + 0x70, z * 65536)
        c = cell_at(x // 16, z // 16)
        put16(uc, c if read32(uc, c) & 0xFFFF == 0 else c + 2, unit_id)
    # Zero-sized unit bounds make splash measurement point-to-point.
    for off in (0x13A, 0x13E, 0x142, 0x146, 0x14A, 0x14E):
        put(uc, K + off, 0)
    put(uc, K + 0x9E, 0)
    uc.mem_write(K + 0x268, bytes(4))

    # The native area pass resolves the one feature at the impact tile.
    put16(uc, cell_at(16, 16) + 8, 0)
    put16(uc, cell_at(16, 16) + 10, 0)
    put(uc, O + 0x130, 0x01000000)
    uc.mem_write(O + 0xFD, b"\0")
    putf(uc, O + 0xE0, 1.0)
    put(uc, O + 0xB8, IF)
    put(uc, IF + 0x80, IF)

    # Profile-derived fields used by 0x529c10 / 0x529dc0 / 0x52a330.
    put(uc, W + 0xB0, 0xFFFFFFFF)
    put(uc, W + 0x6C, 0xFFFFFFFF)
    put(uc, W + 0x70, 0xFFFFFFFF)
    put16(uc, W + 0x8A, aoe)
    putf(uc, W + 0x8C, edge)
    put16(uc, W + 0xB4, 0)
    put(uc, W + 0x40, VT)
    put(uc, VT, 0)
    put(uc, VT + 0x14, VT + 0x14)
    if effect:
        put(uc, W + 0x80, 0x4343)
        put(uc, W + 0x84, 0x4242)
    put(uc, S, W)
    uc.mem_write(S + 4, struct.pack("<3i", 256 * 65536, 10 * 65536, 256 * 65536))
    put(uc, S + 0x7C, O)
    uc.mem_write(S + 0x92, b"\0")
    uc.mem_write(S + 0xD8, bytes(4))
    # Three COM-like references are retired by retail 0x529af0.
    for i, offset in enumerate((0xA8, 0xAC, 0xB0)):
        obj = HEAP + (0xD0000 + i * 0x1000)
        vtable = HEAP + 0xD3000
        put(uc, S + offset, obj)
        put(uc, obj, vtable)
        put(uc, vtable, dtor)
    return p, (G, S, U, STRIDE), logs


def run():
    shipped_profile("arabow", 1, {"type": "ballistic", "model": "araarrow", "default": 476})
    shipped_profile("arapult", 1, {"type": "Ballistic", "areaofeffect": 100,
                                     "edgeeffectiveness": 0.1, "default": 1250})

    p, (_, shot, units, stride), direct = make_probe(476, 0, 1.0, units=((256, 256, 1),))
    victim = units + stride
    _, error = p.call(0x529C10, (shot, victim, 1, 1, 0))
    assert error is None, error
    hit = next(row[1] for row in direct if row[0] == "unit-dispatch")
    assert hit[0] == HEAP + 3 * 0x10000 and hit[1] == victim and hit[2] == 405, hit
    assert read32(p.uc, shot + 0xD8) & 2
    assert len([r for r in direct if r[0] == "release"]) == 3
    print(f"Arabow arrow direct hit: native final damage={hit[2]} from authored 476 (RNG fixed at 0); releases=3; retired=0x{read32(p.uc, shot + 0xD8):x}")

    p, (_, shot, units, stride), area = make_probe(
        1250, 100, 0.1, effect=True,
        units=((256, 256, 1), (281, 256, 1), (308, 256, 1), (260, 256, 0)))
    _, error = p.call(0x529C10, (shot, 0, 1, 1, 0))
    assert error is None, error
    hits = [r[1] for r in area if r[0] == "unit-dispatch"]
    by_id = {((row[1] - units) // stride): row[2] for row in hits}
    assert by_id == {1: 719, 4: 719, 2: 327}, by_id
    assert len([r for r in area if r[0] == "feature-dispatch"]) == 1
    assert len([r for r in area if r[0] == "effect-create"]) == 1
    assert len([r for r in area if r[0] == "release"]) == 3
    assert read32(p.uc, shot + 0xD8) & 2
    print(f"Arapult shell environment impact: native unit damages={by_id} (same-owner unit 4 is included); feature-dispatch=1; effect-create=1; releases=3; retired=0x{read32(p.uc, shot + 0xD8):x}")


if __name__ == "__main__":
    run()
