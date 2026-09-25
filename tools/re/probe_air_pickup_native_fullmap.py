#!/usr/bin/env python3
"""Drive a native VTOL pickup over Lake Lokken through native boarding.

This bounded headless probe combines retail order creation/queue insertion,
the real VTOL_PICKUP mission dispatcher and passenger-target pursuit
controller, native flight mover, map-derived height sectors, native cargo
attachment, and passenger pickup-order removal. It uses the shipped Lake
Lokken TNT height and feature-ID plane. Feature definition bodies, unit
eligibility, mission-name lookup, audio/transfer effects, heap allocation,
and visibility/mover-side map services are controlled fixture boundaries.
The flight follows a direct pursuit goal; this does not test passenger ground
route movement, other aircraft profiles, moving passengers, post-boarding
BECARRIED dispatch, or renderer output. No retail GUI is launched.

Run from the repository root:
    PYTHONPATH=tools/re python3 tools/re/probe_air_pickup_native_fullmap.py
"""
import argparse
import struct
from pathlib import Path

from emuphase import GS, Phase
from check_movement_callback_order import fbi_info
from check_surface_unload_map_release import cat, parse_tnt
from unicorn.x86_const import UC_X86_REG_ECX


MAP = "Lake Lokken"
CARRIER = "zonroc"
PASSENGER = "araarch"
START = (240, 120)
TARGET = (240, 350)
SCALE = 16
BECARRIED_NAME = 0x615984


class PostBoardingMissionBoundary(Exception):
    """Stop before the fixture would have to invent a BECARRIED table row."""


def fixed(value):
    return int(float(value) * 65536)


def u32(uc, address):
    return struct.unpack("<I", uc.mem_read(address, 4))[0]


def i32(uc, address):
    return struct.unpack("<i", uc.mem_read(address, 4))[0]


def put(uc, address, *values):
    uc.mem_write(address, struct.pack("<" + "I" * len(values),
                                      *(value & 0xFFFFFFFF for value in values)))


def byte(uc, address, value):
    uc.mem_write(address, bytes((value & 0xFF,)))


def make_map(phase, root, hpitool):
    """Install TNT-derived cell data and let retail construct height sectors."""
    uc, icd = phase.uc, phase.icd
    map_data = parse_tnt(cat(hpitool, root, "maps.hpi", f"Maps/{MAP}.tnt"))
    width, height, sea, heights, feature_ids, feature_count = map_data
    assert (width, height) == (480, 480), (width, height)
    records = bytearray(width * height * 14)
    for z in range(height):
        for x in range(width):
            index = z * width + x
            offset = index * 14
            corners = (
                heights[index],
                heights[z * width + min(x + 1, width - 1)],
                heights[min(z + 1, height - 1) * width + x],
                heights[min(z + 1, height - 1) * width + min(x + 1, width - 1)],
            )
            records[offset + 4] = heights[index]
            records[offset + 5] = max(corners)
            records[offset + 6] = min(corners)
            struct.pack_into("<H", records, offset + 8, feature_ids[index])
    uc.mem_write(phase.cells_addr, bytes(records))
    put(uc, GS + 0x19E88, width * SCALE)
    put(uc, GS + 0x19E8C, height * SCALE)
    put(uc, GS + 0x19E98, width)
    put(uc, GS + 0x19E9C, height)
    byte(uc, GS + 0x19EF8, sea)
    put(uc, GS + 0x19EC0, feature_count)
    feature_table = phase._alloc(max(320, feature_count * 320))
    put(uc, GS + 0x19EDC, feature_table)
    uc.mem_write(feature_table, bytes(max(320, feature_count * 320)))

    result, error = icd.call(0x50E740)
    if error:
        raise RuntimeError(("retail map height-sector initializer", error))
    sectors = u32(uc, GS + 0x19F18)
    stride = u32(uc, GS + 0x19F1C)
    expected_stride = (width + 7) // 8
    assert stride == expected_stride, (stride, expected_stride)

    # Independently recompute the native sector initializer's raw maxima and
    # 3x3 dilation for this actual shipped TNT plane.
    raw = []
    for sz in range(stride):
        for sx in range(stride):
            raw.append(max(
                sea,
                *(max(heights[z * width + x],
                      heights[z * width + min(x + 1, width - 1)],
                      heights[min(z + 1, height - 1) * width + x],
                      heights[min(z + 1, height - 1) * width +
                              min(x + 1, width - 1)])
                  for z in range(sz * 8, min(height, sz * 8 + 8))
                  for x in range(sx * 8, min(width, sx * 8 + 8)))
            ))
    for sz in range(stride):
        for sx in range(stride):
            expected = max(raw[nz * stride + nx]
                           for nz in range(max(0, sz - 1), min(stride, sz + 2))
                           for nx in range(max(0, sx - 1), min(stride, sx + 2)))
            address = sectors + (sz * stride + sx) * 10
            actual = struct.unpack("<BB", uc.mem_read(address, 2))
            if actual != (raw[sz * stride + sx], expected):
                raise AssertionError(("map height sector", sx, sz, actual,
                                      (raw[sz * stride + sx], expected),
                                      max(heights[z * width + x]
                                          for z in range(sz * 8,
                                                         min(height, sz * 8 + 8))
                                          for x in range(sx * 8,
                                                         min(width, sx * 8 + 8)))))
    return map_data, sectors, stride


def init_orders_and_units(phase, map_data, sectors, stride):
    """Create native orders, reciprocal refs, entity-pool units and movers."""
    uc, icd = phase.uc, phase.icd
    def alloc(_uc, stack):
        size = u32(_uc, stack)
        return 0, phase._alloc(max(size, 16))

    def free(_uc, _stack):
        return 0, 0

    # ``malloc/free`` remain bounded arena sinks. Mission lookup and transport
    # eligibility are game services that require the shipped descriptor/order
    # records or player command path; the test supplies just their answers.
    carrier_kind = phase._alloc(0x400)
    passenger_kind = phase._alloc(0x400)
    # Retail 0x507400's map/footprint scan indexes a per-type byte mask at
    # +0x12a for every cell in the footprint. Mobile FBI records do not expose
    # a yardmap, so the bounded fixture supplies an empty mask plane rather
    # than leaving the native pointer null.
    for kind in (carrier_kind, passenger_kind):
        occupancy_mask = phase._alloc(0x1000)
        put(uc, kind + 0x12A, occupancy_mask)
    owner = phase._alloc(0x400)
    definitions = phase._alloc(25 * 3)
    player_entities = phase._alloc(3 * 0x138)
    carrier, passenger = player_entities + 0x138, player_entities + 2 * 0x138
    carrier_mover, passenger_mover = (phase._alloc(0x400) for _ in range(2))
    carrier_nav = phase._alloc(0x180)
    passenger_nav = phase._alloc(0x180)
    empty_vm_records = []
    carrier_order, passenger_order = (phase._alloc(0x100) for _ in range(2))
    game = GS

    def mission_name(_uc, stack):
        key = u32(_uc, stack)
        destination = _uc.reg_read(UC_X86_REG_ECX)
        if key == BECARRIED_NAME:
            # Native attachment and old-order removal have already happened.
            # Preserve that state, but don't fabricate a descriptor index for
            # the dynamically sorted BECARRIED registry.
            raise PostBoardingMissionBoundary
        # In the loaded native table, code 1 is the carrier pickup handler,
        # code 2 is Move_Seek_Pickup. The VTOL handler asks specifically for
        # the latter when collecting passengers.
        names = {0x604C00: 1, 0x604DE4: 2, 0x604CF8: 2}
        if key not in names:
            raise AssertionError(("unexpected mission name lookup", hex(key),
                                  "tick", u32(_uc, GS + 0x19F44),
                                  "carrier_order", hex(u32(_uc, carrier + 0x60)),
                                  "passenger_order", hex(u32(_uc, passenger + 0x60)),
                                  "cargo_links", hex(u32(_uc, carrier + 0xAC)),
                                  hex(u32(_uc, passenger + 0xA8)),
                                  "carrier_xyz", struct.unpack(
                                      "<3i", _uc.mem_read(carrier + 0x68, 12))))
        byte(_uc, destination, names[key])
        return 1, destination

    def eligible(_uc, stack):
        return 1, int(u32(_uc, stack) == passenger)

    def feedback(_uc, _stack):
        return 3, 0

    def transfer_effect(_uc, _stack):
        counters[0] += 1
        return 2, 0

    counters = [0]
    icd.hooks.update({
        0x4EB9E0: alloc,
        0x4EBA00: free,
        0x4D4BF0: mission_name,
        0x519F50: eligible,
        0x535CC0: lambda _uc, _stack: (1, 0),
        0x50A9C0: feedback,
        0x421E10: transfer_effect,
        # Native model script requests are absent in this bounded no-renderer
        # unit fixture; transport logic/mover stay retail.
        0x56C640: lambda _uc, _stack: (8, 0),
    })

    put(uc, 0x62D55C, game)
    put(uc, 0x62DB84, definitions)
    put(uc, game + 0x19F30, 1)
    put(uc, game + 0x174C8, 101)
    put(uc, game + 0x174CC, 102)
    put(uc, definitions + 25 + 4, 0x41A680)
    put(uc, definitions + 25 + 0x11, 0x200)
    put(uc, definitions + 50 + 4, 0x403430)
    put(uc, definitions + 50 + 0x11, 0x200)

    # Native entity resolver used by 0x51b4f0 -> 0x51b5a0 (IDs 1 and 2).
    put(uc, game + 0x14E84, player_entities)
    put(uc, game + 0x14E88, passenger)
    put(uc, owner, 1)
    byte(uc, owner + 0xEA, 1)
    put(uc, owner + 0x74, carrier)
    put(uc, owner + 0x78, passenger + 0x138)

    carrier_info = fbi_info(CARRIER)
    passenger_info = fbi_info(PASSENGER)
    assert carrier_info.get("canfly") == "1"
    assert carrier_info.get("cantransport") == "1"
    assert passenger_info.get("movementclass", "").upper().startswith("GROUND")
    for unit, kind, unit_id, info in (
            (carrier, carrier_kind, 1, carrier_info),
            (passenger, passenger_kind, 2, passenger_info)):
        put(uc, unit + 2, unit_id)
        put(uc, unit + 0xB4, kind)
        put(uc, unit + 0xB8, owner)
        put(uc, unit + 0x130, 0x01000000)
        put(uc, unit + 0x78,
            int(float(info.get("footprintx", "1"))) |
            (int(float(info.get("footprintz", "1"))) << 16))
        byte(uc, kind + 0x24B, 0)
        # The ordinary dispatcher asks the unit's COB object whether it
        # declares certain callbacks. A native VM with zero methods makes the
        # real 0x56c4a0 lookup return its retail "not found" sentinel while
        # avoiding a fake unit+0xbc pointer or substituting the method lookup.
        empty_vm, empty_descriptor = phase._alloc(0x40), phase._alloc(0x40)
        put(uc, unit + 0xBC, empty_vm)
        put(uc, empty_vm + 0x0C, empty_descriptor)
        put(uc, empty_descriptor + 4, 0)
        empty_vm_records.append((empty_vm, empty_descriptor))

    # Use authored ZONROC movement/flight/transport fields. The tests only
    # control native host calls; all flight calculations are retail code.
    put(uc, carrier + 8, carrier_mover)
    put(uc, carrier + 0x12B, fixed(carrier_info["maxvelocity"]))
    put(uc, carrier_kind + 0x162, fixed(carrier_info["maxvelocity"]))
    put(uc, carrier_kind + 0x166, fixed(carrier_info["brakerate"]))
    put(uc, carrier_kind + 0x16A, fixed(carrier_info["acceleration"]))
    put(uc, carrier_kind + 0x16E, fixed(carrier_info.get("watermultiplier", "1")))
    put(uc, carrier_kind + 0x172, fixed(carrier_info.get("roadmultiplier", "1.2")))
    put(uc, carrier_kind + 0x182, fixed(carrier_info.get("moverate1", "1")))
    put(uc, carrier_kind + 0x186, fixed(carrier_info.get("moverate2", "9")))
    put(uc, carrier_kind + 0x23E, int(float(carrier_info["transportdistance"])))
    put(uc, carrier_kind + 0x260, 0x800)
    put(uc, carrier_kind + 0x264, 0x200)
    uc.mem_write(carrier_kind + 0x23A,
                 struct.pack("<h", int(float(carrier_info["cruisealt"]))))
    put(uc, carrier_kind + 0x126,
        int(float(carrier_info.get("footprintx", "1"))) |
        (int(float(carrier_info.get("footprintz", "1"))) << 16))
    uc.mem_write(carrier_kind + 0x18E,
                 struct.pack("<H", int(float(carrier_info["turnrate"]))))
    byte(uc, carrier_kind + 0x249, 70)
    put(uc, carrier_kind + 0x182, fixed(carrier_info.get("moverate1", "1")))
    put(uc, carrier_kind + 0x186, fixed(carrier_info.get("moverate2", "9")))

    width, height, sea, heights, _features, _feature_count = map_data
    for unit, mover, position, info in (
            (carrier, carrier_mover, START, carrier_info),
            (passenger, passenger_mover, TARGET, passenger_info)):
        x, z = position
        ground_y = heights[z * width + x]
        y = ground_y + (int(float(info["cruisealt"])) if unit == carrier else 0)
        put(uc, unit + 0x68, x * SCALE * 65536, y * 65536,
            z * SCALE * 65536)
        uc.mem_write(unit + 0x74, struct.pack("<hh", x, z))
        put(uc, unit + 8, mover)
    # The map positions are within the same shipped height sector grid. Both
    # movers begin stopped; the flight controller is native pursuit.
    put(uc, passenger_mover, passenger_nav)
    put(uc, passenger_mover + 0x20, 0)
    put(uc, passenger_nav, 0x5F2A24)
    put(uc, passenger_nav + 8, passenger)
    put(uc, carrier_mover, carrier_nav)
    put(uc, carrier_mover + 0x20, 0)
    put(uc, carrier_mover + 0x30, 0)
    uc.mem_write(carrier_mover + 0x36, struct.pack("<H", 2))
    put(uc, carrier_nav, 0x5F34D4)
    put(uc, carrier_nav + 8, carrier)
    x, z = START
    ground_y = heights[z * width + x]
    put(uc, carrier_nav + 0x0C,
        x * SCALE * 65536, (ground_y + int(float(carrier_info["cruisealt"]))) * 65536,
        z * SCALE * 65536)
    uc.mem_write(carrier_nav + 0x24, b"\0\0")

    # Current sector membership is the same native 128px sector/list layout
    # used by 0x4dc800 and the attachment routine.
    for unit, position in ((carrier, START), (passenger, TARGET)):
        sx, sz = position[0] // 8, position[1] // 8
        sector = sectors + (sz * stride + sx) * 10
        _, error = icd.call(0x506650, (unit, sector))
        if error:
            raise RuntimeError(("native sector insertion", unit, error))
        assert u32(uc, unit + 0xA4) == sector

    # Actual native code-2 Move_Seek_Pickup and code-1 carrier VTOL_PICKUP
    # orders. The passenger order's carrier pointer becomes a reciprocal
    # reference during insertion, and the carrier order references the target.
    p_order = phase._alloc(0x100)
    c_order = phase._alloc(0x100)
    for code, target, order, unit in (
            (2, carrier, p_order, passenger),
            (1, passenger, c_order, carrier)):
        result, error = icd.call(
            0x4D6C40, (code, target, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
            ecx=order)
        if error or result != order:
            raise RuntimeError(("native transport order constructor", code,
                                result, error))
        _, error = icd.call(0x4D7750, (unit, order))
        if error:
            raise RuntimeError(("native transport order queue insertion", code,
                                error))
    assert u32(uc, passenger + 0x60) == p_order
    assert u32(uc, carrier + 0x60) == c_order
    assert u32(uc, carrier + 0xC4) == p_order + 0x12
    assert u32(uc, passenger + 0xC4) == c_order + 0x12
    return {
        "icd": icd, "uc": uc, "carrier": carrier, "passenger": passenger,
        "carrier_kind": carrier_kind, "passenger_kind": passenger_kind,
        "carrier_mover": carrier_mover, "passenger_mover": passenger_mover,
        "carrier_nav": carrier_nav, "carrier_order": c_order,
        "passenger_order": p_order, "owner": owner, "definitions": definitions,
        "map_effects": counters,
    }


def run(args):
    root = Path(args.retail_root).resolve()
    phase = Phase(480, 480)
    map_data, sectors, stride = make_map(phase, root, Path(args.hpitool).resolve())
    live = init_orders_and_units(phase, map_data, sectors, stride)
    icd, uc = live["icd"], live["uc"]
    carrier, passenger = live["carrier"], live["passenger"]
    carrier_mover, carrier_nav = live["carrier_mover"], live["carrier_nav"]
    carrier_order, passenger_order = live["carrier_order"], live["passenger_order"]
    effects = live["map_effects"]
    initial_position = struct.unpack("<3i", uc.mem_read(carrier + 0x68, 12))

    settings = phase._alloc(0x1000)
    options = settings + 0x100
    put(uc, 0x62D558, settings)
    put(uc, settings, options)
    put(uc, settings + 8, options)
    uc.mem_write(options, bytes(0x100))

    # Start at a real Lake Lokken cell and follow the stationary Araarch via
    # retail's pursuit controller. Each tick is the ordinary dispatcher/mover
    # order: one native carrier mission dispatch, then the real flight update.
    tick = 0
    rows = []
    controller_seen = False
    attachment_tick = None
    selected = False
    postboarding_boundary = False
    for _step in range(args.max_ticks):
        tick += 1
        put(uc, GS + 0x19F44, tick)
        try:
            _, error = icd.call(0x4D8450, (carrier,))
        except PostBoardingMissionBoundary:
            postboarding_boundary = True
            attachment_tick = tick
            position = struct.unpack("<3i", uc.mem_read(carrier + 0x68, 12))
            controller = u32(uc, carrier_nav + 4)
            stage = uc.mem_read(carrier_order + 5, 1)[0] \
                if u32(uc, carrier + 0x60) == carrier_order else -1
            events = u32(uc, carrier_order + 0x6A) \
                if u32(uc, carrier + 0x60) == carrier_order else 0
            rows.append((tick, *position, stage, events, controller,
                         u32(uc, carrier + 0xAC), u32(uc, passenger + 0xA8)))
            break
        if error:
            raise RuntimeError(("native VTOL pickup dispatcher", tick, error))
        controller = u32(uc, carrier_nav + 4)
        if controller:
            controller_seen = True
            selected = True
        _, error = icd.call(0x4DC800, (carrier,), ecx=carrier_mover)
        if error:
            raise RuntimeError(("native flight mover", tick, error))
        # The ordinary unit-update cadence follows 0x4dc800 with 0x51b2a0.
        # For this flying class it should preserve airborne height unless the
        # native dirty/hover conditions request a ground-supported update.
        _, error = icd.call(0x51B2A0, (carrier,), ecx=carrier_mover)
        if error:
            raise RuntimeError(("native flight height commit", tick, error))
        position = struct.unpack("<3i", uc.mem_read(carrier + 0x68, 12))
        stage = uc.mem_read(carrier_order + 5, 1)[0] \
            if u32(uc, carrier + 0x60) == carrier_order else -1
        events = u32(uc, carrier_order + 0x6A) \
            if u32(uc, carrier + 0x60) == carrier_order else 0
        controller = u32(uc, carrier_nav + 4)
        if u32(uc, carrier + 0xAC) == passenger and \
                u32(uc, passenger + 0xA8) == carrier:
            attachment_tick = tick
        rows.append((tick, *position, stage, events, controller,
                     u32(uc, carrier + 0xAC), u32(uc, passenger + 0xA8)))
        if attachment_tick is not None:
            break
    if not selected or not controller_seen:
        raise AssertionError("native VTOL pickup never installed pursuit controller")
    if attachment_tick is None:
        if args.allow_incomplete and not postboarding_boundary:
            final_position = struct.unpack("<3i", uc.mem_read(carrier + 0x68, 12))
            if final_position == initial_position:
                raise AssertionError(("bounded native mover made no progress",
                                      final_position))
            if u32(uc, carrier + 0x60) != carrier_order or \
                    u32(uc, passenger + 0x60) != passenger_order:
                raise AssertionError("orders changed before the early-stop control")
            if u32(uc, carrier + 0xAC) or u32(uc, passenger + 0xA8):
                raise AssertionError("cargo attached before the early-stop control")
            print(f"PASS: {args.max_ticks} bounded native pursuit ticks moved "
                  f"ZONROC toward stationary Araarch; controller remains "
                  f"installed and both orders remain queued. Boarding is "
                  "correctly outside this short control window.")
            return
        raise AssertionError(("native air pickup failed before attachment", rows[-8:]))
    if not postboarding_boundary:
        raise AssertionError(("native pickup did not reach post-boarding descriptor seam",
                              attachment_tick, rows[-1]))
    if u32(uc, carrier + 0xAC) != passenger or u32(uc, passenger + 0xA8) != carrier:
        raise AssertionError("native cargo links are not reciprocal")
    if u32(uc, passenger + 0x60) != 0:
        raise AssertionError(("native boarding did not remove passenger pickup order",
                              hex(u32(uc, passenger + 0x60))))
    if u32(uc, carrier + 0x60) != carrier_order:
        raise AssertionError(("carrier pickup order retired before boarding sample",
                              hex(u32(uc, carrier + 0x60))))
    assert u32(uc, carrier + 0xAC) == passenger
    assert u32(uc, passenger + 0xA8) == carrier
    assert effects[0] >= 2, effects
    print(
        f"PASS: Lake Lokken native VTOL pickup selected {PASSENGER} with real "
        f"code-1/code-2 orders, installed native pursuit, completed "
        f"{len(rows) - int(postboarding_boundary)} native flight updates over "
        f"the map-built sectors, attached the passenger, and removed its code-2 "
        f"pickup order. The carrier code-1 order remains active at this seam; "
        f"native BECARRIED name lookup is reached and intentionally left "
        f"unresolved rather than assigned a fabricated descriptor code. "
        f"boarding dispatcher tick {attachment_tick}; "
        f"map sectors {stride}x{stride}, transfer effects {effects[0]}."
    )
    print("  Carrier final XYZ:", tuple(value // 65536 for value in
                                           struct.unpack("<3i", uc.mem_read(carrier + 0x68, 12))))
    print("  Controlled boundaries: feature-definition bodies; mission-name and "
          "passenger-eligibility lookup; allocator/free; audio/effect/UI and "
          "script-VM sinks. Unit order, carrier mission handlers, flight "
          "controller/mover, map-sector construction, cargo attachment, and "
          "passenger pickup-order removal execute from KINGDOMS.icd. Native "
          "dispatch reaches the BECARRIED descriptor lookup, whose name-sorted "
          "registry is deliberately not approximated here.")


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retail-root", default="/home/pocket_geek/tak_data")
    parser.add_argument("--hpitool", default=str(repo / "build-o2/hpitool"))
    parser.add_argument("--max-ticks", type=int, default=5000)
    parser.add_argument("--allow-incomplete", action="store_true",
                        help="accept a moving native pursuit that has not yet boarded at the tick limit")
    args = parser.parse_args()
    if args.max_ticks < 1 or args.max_ticks > 10000:
        parser.error("--max-ticks must be in 1..10000")
    run(args)


if __name__ == "__main__":
    main()
