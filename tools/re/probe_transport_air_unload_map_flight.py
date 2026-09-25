#!/usr/bin/env python3
"""Pair a map-backed native VTOL-unload mission with World through release.

The World fixture flies across a 100-to-220 height ridge. Retail builds its
height sectors from the same per-cell records with 0x50e740, keeps the real
0x4dc800 terrain scan active, and runs the native VTOL_UNLOAD dispatcher,
circle controller, mover, arrival, transfer, PARK callback, and mission tail.
Only landing feasibility, effect creation, cargo detach, and PARK installation
remain the established native host-boundary hooks.
"""
import argparse
import struct
import subprocess

from emu import HEAP
from check_air_map_height_scan import populate_native_map, random_draws_from_world
from probe_transport_air_unload_flight import (
    MAP_WIDTH, SECTOR_STRIDE, native_row, prepare_native,
)


def heightstep_world_trace(binary, steps):
    output = subprocess.run(
        [binary, "--air-unload-heightstep-trace", str(steps)],
        check=True, capture_output=True, text=True,
    ).stdout
    rows = [tuple(map(int, line.split())) for line in output.splitlines()]
    if len(rows) != steps or any(len(row) != 32 for row in rows):
        raise AssertionError(("World trace shape", len(rows), steps,
                              sorted({len(row) for row in rows})))
    return rows


def configure_live_scans(native):
    settings = HEAP + 0x1E0000
    options = settings + 0x1000
    native.put(0x62D558, settings)
    native.put(settings, options)
    native.put(settings + 8, options)
    native.p.uc.mem_write(options, bytes(0x100))
    # The native mover polls its own local terrain-scan deadline every 70 ticks.
    native.byte(native.kind + 0x249, 70)
    native.put(native.mover + 0x30, 0)
    return settings, options


def run_native(native, world_rows, max_steps, sectors, stride):
    p = native.p
    uc = p.uc
    draws = random_draws_from_world(world_rows)
    native.randomValue = lambda bound, tick: (
        draws.get(tick, 0) if bound == 6 else 0
    )
    settings, options = configure_live_scans(native)

    sector_end = sectors + stride * SECTOR_STRIDE * 10
    sector_heights_before = b"".join(
        bytes(uc.mem_read(sectors + index * 10, 4))
        for index in range(stride * SECTOR_STRIDE)
    )
    rows = []
    scan_deadlines = []
    relinks = 0
    previous_pointer = native.get(native.carrier + 0xA4)
    controller_address = 0
    terminal_reached = False

    for tick in range(1, min(max_steps, len(world_rows)) + 1):
        native.dispatch(tick)
        # The dispatcher uses this global context; restore the enabled options
        # before the actual native mover performs its live terrain scan.
        native.put(0x62D558, settings)
        native.put(settings, options)
        native.put(settings + 8, options)
        _, error = p.call(0x4DC800, (native.carrier,), ecx=native.mover)
        if error:
            raise RuntimeError(("native map-backed air mover", tick, error))

        controller = native.get(native.nav + 4)
        if controller and not controller_address:
            controller_address = controller
            # The 0x50e740 sector plane and the subsequently allocated circle
            # controller must be disjoint; a collision here would invalidate
            # every later movement sample.
            if controller < sector_end and controller + 0x100 > sectors:
                raise AssertionError(("circle controller overlaps map sectors",
                                      hex(controller), hex(sectors), hex(sector_end)))

        position = struct.unpack("<3i", uc.mem_read(native.carrier + 0x68, 12))
        sector_x, sector_z = position[0] >> 23, position[2] >> 23
        expected_pointer = sectors + (sector_z * stride + sector_x) * 10
        actual_pointer = native.get(native.carrier + 0xA4)
        if actual_pointer != expected_pointer:
            raise AssertionError(("native height-sector relink", tick,
                                  (sector_x, sector_z), hex(actual_pointer),
                                  hex(expected_pointer)))
        if actual_pointer != previous_pointer:
            relinks += 1
            previous_pointer = actual_pointer

        rows.append(native_row(native, tick))
        deadline = native.get(native.mover + 0x30)
        if not scan_deadlines or deadline != scan_deadlines[-1]:
            scan_deadlines.append(deadline)

        # Compare up to the first completed PARK callback and retired mission;
        # continuing after PARK finishes would compare a latched native hook
        # against World's current queue head instead of the same event.
        row = rows[-1]
        if not row[16] and not row[28] and not row[29] and row[31]:
            terminal_reached = True
            break

    sector_heights_after = b"".join(
        bytes(uc.mem_read(sectors + index * 10, 4))
        for index in range(stride * SECTOR_STRIDE)
    )
    if sector_heights_after != sector_heights_before:
        raise AssertionError("native mover/mission modified map height-sector planes")
    if not rows or not controller_address:
        raise AssertionError(("native unload did not create its circle controller",
                              controller_address, len(rows)))
    if not terminal_reached:
        raise AssertionError(("mission did not reach released/parked terminal state",
                              max_steps, rows[-1]))
    if len(scan_deadlines) < 3 or not relinks:
        raise AssertionError(("live map scan/relink path not exercised",
                              scan_deadlines, relinks))
    return rows, relinks, scan_deadlines, controller_address


def compare(binary, max_steps):
    world_rows = heightstep_world_trace(binary, max_steps)
    native = prepare_native()

    # 0x50e740 receives the same height map as World's x>=220 ridge. The map
    # initializer runs before dispatch creates the native circle controller.
    if native.get(native.nav + 4):
        raise AssertionError("unexpected active controller before native map init")
    sectors, stride = populate_native_map(native)
    if stride != SECTOR_STRIDE:
        raise AssertionError(("native map-sector stride", stride, SECTOR_STRIDE))

    # Confirm allocator state and active controller do not overlap map sectors.
    allocator = native.p.hooks[0x4EB9E0]
    cursor_box = allocator.__closure__[0].cell_contents
    map_allocation_cursor = cursor_box[0]
    sector_end = sectors + stride * SECTOR_STRIDE * 10
    if map_allocation_cursor < sector_end:
        raise AssertionError(("map allocator cursor overlaps sector table",
                              hex(map_allocation_cursor), hex(sector_end)))

    native_rows, relinks, scan_deadlines, controller = run_native(
        native, world_rows, max_steps, sectors, stride)
    if not native_rows or native_rows[-1][16] or native_rows[-1][28] or \
            native_rows[-1][29] or not native_rows[-1][31]:
        raise AssertionError(("native terminal state", native_rows[-1]))

    arrival_pending_samples = 0
    for index, (world, retail) in enumerate(zip(world_rows, native_rows), 1):
        if world == retail:
            continue
        differences = [i for i, (a, b) in enumerate(zip(world, retail)) if a != b]
        # World exposes the arrival event after its mover pass. Retail posts it
        # in the mover and consumes it on the next dispatcher pass.
        if (differences == [20] and world[17] == retail[17] == 0 and
                world[20] == 0x500 and retail[20] == 0):
            arrival_pending_samples += 1
            continue
        raise AssertionError({"tick": index, "World": world, "retail": retail})
    if arrival_pending_samples != 1:
        raise AssertionError(("arrival wake sampling count", arrival_pending_samples))
    if len(world_rows) < len(native_rows):
        raise AssertionError(("World trace ended before native terminal",
                              len(world_rows), len(native_rows)))
    world_terminal = world_rows[len(native_rows) - 1]
    if world_terminal[16] or world_terminal[28] or world_terminal[29] or \
            not world_terminal[31]:
        raise AssertionError(("World terminal state", world_terminal))

    release_tick = next(row[0] for row in native_rows if not row[28] and not row[29])
    park_tick = next(row[0] for row in native_rows if row[31])
    retirement_tick = next(row[0] for row in native_rows if not row[16])
    arrival_tick = next(
        index for index, (world, retail) in enumerate(
            zip(world_rows, native_rows), 1)
        if world[20] == 0x500 and retail[20] == 0
    )
    if not release_tick <= park_tick <= retirement_tick:
        raise AssertionError(("unexpected release/PARK/retirement order",
                              release_tick, park_tick, retirement_tick))

    print(
        f"PASS: {len(native_rows)} paired map-backed VTOL-unload ticks match "
        f"through ridge flight, arrival, cargo release, PARK callback, and "
        f"mission retirement; native controller {controller:#x} is disjoint "
        f"from the 0x50e740 sector table, {relinks} sector relinks, "
        f"{len(scan_deadlines) - 1} live scan-deadline advances "
        f"({scan_deadlines[0]}..{scan_deadlines[-1]}), and one expected "
        f"post-mover 0x500 sampling difference at tick {arrival_tick}; "
        f"release/PARK/mission-retirement ticks {release_tick}/{park_tick}/"
        f"{retirement_tick}"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="build-o2/transport_test")
    parser.add_argument("--steps", type=int, default=600,
                        help="maximum World/native trace length (capped at 1200)")
    args = parser.parse_args()
    compare(args.binary, max(1, min(args.steps, 1200)))


if __name__ == "__main__":
    main()
