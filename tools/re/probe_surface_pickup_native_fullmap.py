#!/usr/bin/env python3
"""Continue a native Lake Lokken sea pickup over full TNT grades and real IDs.

Unlike the corridor probe, this grades all 480x480 TNT cells with retail's
0x508cd0 and advances the real Vertrans mover on those map records. Its Araarch
and Vertrans records live in the native game's global contiguous 0x138-byte
entity pool, so 0x51b4f0 -> 0x51b5a0 resolves their IDs through the real lookup.
Only mover-side game services, player feedback, and transfer effects remain
controlled boundaries. No retail GUI is launched.

Run from the repository root:
    PYTHONPATH=tools/re python3 tools/re/probe_surface_pickup_native_fullmap.py
"""
import argparse
import struct
from pathlib import Path

from emuphase import GS
from probe_surface_pickup_native_map_route import (
    CARRIER, PASSENGER, START, TARGET, run as run_route,
)
from probe_surface_pickup_native_mission import run_native, run_world
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP


def install_native_entity_array(state):
    """Move the live fixture units into retail's ID-indexed global entity pool.

    Native 0x51b5a0 computes game+0x14e84 + 312*unitId, rejects addresses
    beyond game+0x14e88, then checks the unit's alive bit at +0x130. Entity ID
    0 is reserved here; Vertrans is ID 1 and Araarch is ID 2.
    """
    phase, live = state["phase"], state["live"]
    uc = phase.uc
    old_carrier, old_passenger = live.carrier, live.passenger
    pool = phase._alloc(3 * 0x138)
    carrier, passenger = pool + 0x138, pool + 2 * 0x138

    for unit_id, src, dst in ((1, old_carrier, carrier),
                              (2, old_passenger, passenger)):
        uc.mem_write(dst, bytes(uc.mem_read(src, 0x138)))
        live.put(dst + 2, unit_id)

    # This is the exact lookup layout read by the native attachment routine.
    live.put(GS + 0x14E84, pool)
    live.put(GS + 0x14E88, passenger)  # inclusive last entity address

    # Preserve the existing native mission/controller graph while rebasing
    # pointers that referred to the original heap fixture units.
    live.carrier, live.passenger = carrier, passenger
    live.put(live.mission + 0x0E, carrier)
    live.put(live.mission + 0x16, passenger)
    live.put(live.passenger_mission + 0x16, carrier)
    live.put(live.nav + 8, carrier)

    # Replace the synthetic player fixture with game slot 0. The attached-unit
    # helper tests this record's first dword and then may inspect +0xea; retail's
    # worker fixture populated the dword with a non-pointer sentinel, so clear
    # it after route delivery. Keep the actual one-player marker and a valid,
    # empty player-owned entity range for the transfer handler's fallback scan.
    owner = GS + 0x2404
    live.owner = owner
    live.put(carrier + 0xB8, owner)
    live.put(passenger + 0xB8, owner)
    live.put(owner, 0)
    live.byte(owner + 0xEA, 1)
    live.put(owner + 0x74, pool + 0x138)
    live.put(owner + 0x78, pool + 0x138 - 1)

    assert live.get(GS + 0x14E84) == pool
    assert live.get(GS + 0x14E88) == passenger
    assert live.get(carrier + 2) & 0xFFFF == 1
    assert live.get(passenger + 2) & 0xFFFF == 2
    assert live.get(carrier + 0x130) & 0x01000000
    assert live.get(passenger + 0x130) & 0x01000000
    assert 0x51B4F0 not in phase.icd.hooks, "native attachment was intercepted"
    return pool, carrier, passenger


def observe_attachment_calls(state):
    """Observe, without replacing, both native attachment entrypoints."""
    uc = state["phase"].uc
    calls = []

    def on_code(machine, address, _size, _user):
        if address not in (0x51B4F0, 0x51B5A0):
            return
        esp = machine.reg_read(UC_X86_REG_ESP)
        if address == 0x51B4F0:
            args = struct.unpack("<5I", machine.mem_read(esp + 4, 20))
            calls.append((address, args))
        else:
            record, mode = struct.unpack("<II", machine.mem_read(esp + 4, 8))
            payload = bytes(machine.mem_read(record, 7))
            calls.append((address, (record, mode, payload)))

    uc.hook_add(UC_HOOK_CODE, on_code,
                begin=0x51B4F0, end=0x51B5A0)
    return calls


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retail-root", default="/home/pocket_geek/tak_data")
    parser.add_argument("--hpitool", default=str(repo / "build-o2/hpitool"))
    parser.add_argument("--world-binary", default=str(repo / "build-o2/transport_test"))
    parser.add_argument("--max-ticks", type=int, default=5000)
    args = parser.parse_args()

    retail_root = Path(args.retail_root).resolve()
    state = run_route(retail_root, Path(args.hpitool).resolve(),
                      full_map=True, native_attachment=True)
    pool, carrier, passenger = install_native_entity_array(state)
    state["native_attachment"] = True
    calls = observe_attachment_calls(state)
    native_tick, arrival_tick, native_trace = run_native(state, args.max_ticks)

    native_outer = [entry for entry in calls if entry[0] == 0x51B4F0]
    native_inner = [entry for entry in calls if entry[0] == 0x51B5A0]
    assert len(native_outer) == 1, native_outer
    assert len(native_inner) == 1, native_inner
    assert native_outer[0][1][:2] == (passenger, carrier), native_outer
    assert native_outer[0][1][2:] == (0xFFFFFFFF, 0, 1), native_outer
    assert native_inner[0][1][1:] == (1, bytes((7, 2, 0, 1, 0, 0xFF, 0))), native_inner

    world_tick, world_output = run_world(Path(args.world_binary).resolve(), retail_root)
    cargo = state["live"].get(carrier + 0xAC)
    parent = state["live"].get(passenger + 0xA8)
    passenger_mission = state["live"].get(passenger + 0x60)
    passenger_flags = state["live"].get(passenger + 0x130)
    assert passenger_mission == state["live"].passenger_mission, (
        "retail attachment unexpectedly changed the fixture passenger order",
        hex(passenger_mission))
    print(
        f"PASS: retail-grade Lake Lokken sea pickup covered all "
        f"{state['width'] * state['height']} TNT cells; the native worker "
        f"installed {len(state['route'])} route waypoints at tick "
        f"{state['delivered_at']}, then Vertrans arrived at tick "
        f"{arrival_tick} and attached Araarch at tick {native_tick} "
        f"({native_tick - state['delivered_at']} mover ticks after delivery)."
    )
    print(
        f"  Real entity array: base {pool:#010x}, inclusive end "
        f"{state['live'].get(GS + 0x14E88):#010x}, IDs 1/2 at "
        f"{carrier:#010x}/{passenger:#010x}; native 0x51b4f0 and 0x51b5a0 "
        "both executed once."
    )
    print(
        f"  Native terminal state: cargo={cargo:#010x}, reciprocal parent="
        f"{parent:#010x}, passenger order pointer={passenger_mission:#010x}, "
        f"passenger flags={passenger_flags:#010x}, "
        f"transfer effects={state['live'].transfer_effects}."
    )
    print(
        f"  World full-map shipped-map roundtrip boarded at tick {world_tick} "
        "and completed shore unload/placement/coast. Native ticks are not "
        "asserted equal to World ticks."
    )
    assert "PASS: boat completed routed shore unload on the shipped map" in world_output


if __name__ == "__main__":
    main()
