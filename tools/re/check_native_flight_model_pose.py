#!/usr/bin/env python3
"""Join a native flyer mover/COB trace to the shipped 3DO geometry.

This focused VERBALL check calls retail's 0x4dc800 mover with a controlled
flight sequence, dispatches the resulting callbacks through the attached
shipped COB VM, and captures the post-tick piece poses. It replays the exact
accepted callback schedule through the World script oracle, then compares all
native 0x4ee620 model vertices with model_transform_test. This is a headless
geometry/state check, not a camera, texture, projection, or framebuffer test.
"""
import argparse
import struct
import subprocess
from pathlib import Path

from emu import HEAP, Icd
from check_movement_callback_order import fbi_info
from probe_native_mover_cob_transitions import local_trace, native_transition_trace


ROOT = Path(__file__).resolve().parents[2]
UNIT = "verball"
COB_PATH = ROOT / "assets/extracted/all/scripts/verball.cob"
MODEL_PATH = ROOT / "assets/extracted/all/objects3d/verball.3do"
SAMPLE_TICK = 5
WING_PIECES = ("wing1Left", "wing1Right", "wing2Left", "wing2Right")


def signed(value):
    return struct.unpack("<i", struct.pack("<I", value & 0xFFFFFFFF))[0]


def names_at(data, offset, count):
    names = []
    for index in range(count):
        pointer = struct.unpack_from("<I", data, offset + 4 * index)[0]
        names.append(data[pointer:data.index(b"\0", pointer)].decode("ascii"))
    return names


def parse_object(data, offset):
    count = struct.unpack_from("<I", data, offset + 4)[0]
    name_pointer = struct.unpack_from("<I", data, offset + 28)[0]
    name = data[name_pointer:data.index(b"\0", name_pointer)].decode("ascii")
    authored_offset = struct.unpack_from("<3i", data, offset + 16)
    vertex_offset = struct.unpack_from("<I", data, offset + 36)[0]
    vertices = [struct.unpack_from("<3i", data, vertex_offset + 12 * i)
                for i in range(count)]
    child = struct.unpack_from("<I", data, offset + 48)[0]
    children = []
    while child:
        sibling = struct.unpack_from("<I", data, child + 44)[0]
        children.append(parse_object(data, child))
        child = sibling
    return {"name": name, "offset": authored_offset,
            "vertices": vertices, "children": children}


def compare_geometry(pose, heading, binary):
    cob_data = COB_PATH.read_bytes()
    model_data = MODEL_PATH.read_bytes()
    header = struct.unpack_from("<10I", cob_data)
    piece_names = names_at(cob_data, header[8], header[2])
    piece_index = {name.lower(): index for index, name in enumerate(piece_names)}
    root = parse_object(model_data, 0)

    p = Icd()
    uc = p.uc
    definition_cursor = HEAP + 0x240000
    state_cursor = HEAP + 0x280000
    output_cursor = HEAP + 0x2A0000
    output_vertices = []
    world_rows = []
    model_pieces = []

    def put(address, *values):
        uc.mem_write(address, struct.pack("<" + "I" * len(values),
                                         *(value & 0xFFFFFFFF for value in values)))

    def build(obj, ancestors):
        nonlocal definition_cursor, state_cursor, output_cursor
        definition = definition_cursor
        definition_cursor += 0x60
        authored_vertices = definition_cursor
        definition_cursor += max(12, len(obj["vertices"]) * 12)
        output = output_cursor
        output_cursor += max(12, len(obj["vertices"]) * 12)
        state = state_cursor
        state_cursor += 0x40

        x, y, z = obj["offset"]
        put(definition + 4, len(obj["vertices"]))
        put(definition + 0x10, -x, y, -z)
        put(definition + 0x24, authored_vertices)
        if obj["vertices"]:
            uc.mem_write(authored_vertices, b"".join(
                struct.pack("<3i", -vx, vy, -vz) for vx, vy, vz in obj["vertices"]))

        put(state, definition)
        put(state + 0x24, output)
        put(state + 0x28, 0)  # invalidate the cached piece transform
        piece = piece_index.get(obj["name"].lower())
        current = pose[piece] if piece is not None else [0] * 6
        uc.mem_write(state + 4, struct.pack("<3i", *(signed(v) for v in current[:3])))
        uc.mem_write(state + 0x10, struct.pack("<3H", *(v & 0xFFFF for v in current[3:])))
        model_pieces.append(obj["name"])

        chain = ancestors + [(obj["offset"], current)]
        for vertex_index, vertex in enumerate(obj["vertices"]):
            row = []
            for offset, current_pose in chain:
                row.extend(offset)
                row.extend(current_pose[:3])
                row.extend(current_pose[3:])
                row.extend((0, 0, 0))  # authored vertex, filled at the leaf
                row.extend((0, 0, 0))  # root body pitch, heading, roll
            leaf_base = (len(chain) - 1) * 15
            row[leaf_base + 9:leaf_base + 12] = vertex
            row[12:15] = (0, heading, 0)
            world_rows.append(" ".join(map(str, (len(chain), *row))))
            output_vertices.append((output + vertex_index * 12,
                                    obj["name"], vertex_index))

        children = [build(child, chain) for child in obj["children"]]
        if children:
            put(state + 0x30, children[0]["state"])
        for left, right in zip(children, children[1:]):
            put(left["state"] + 0x2C, right["state"])
        return {"state": state}

    instance = build(root, [])
    wrapper = HEAP + 0x230000
    unit = HEAP + 0x50000
    put(unit + 0xC0, wrapper)
    put(wrapper + 8, 1)
    put(wrapper + 0x1C8, instance["state"])
    uc.mem_write(unit + 0x7C, struct.pack("<3H", 0, heading, 0))
    _, error = p.call(0x4EE620, (unit,))
    if error:
        raise RuntimeError(("native 3DO refresh", error))

    actual = subprocess.run([binary, "--chain"], input="\n".join(world_rows) + "\n",
                            text=True, capture_output=True, check=True)
    world_vertices = [tuple(map(float, line.split()))
                      for line in actual.stdout.splitlines()]
    if len(world_vertices) != len(output_vertices):
        raise AssertionError(("transformed vertex count", len(world_vertices),
                              len(output_vertices)))
    errors = []
    for address, _, _ in output_vertices:
        native = tuple(value / 65536.0 for value in
                       struct.unpack("<3i", uc.mem_read(address, 12)))
        expected = world_vertices[len(errors)]
        errors.append(max(abs(a - b) for a, b in zip(native, expected)))
    worst = max(range(len(errors)), key=errors.__getitem__)
    return len(model_pieces), len(output_vertices), errors[worst], output_vertices[worst]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--script-binary", default="build-o2/retail_script_test")
    parser.add_argument("--model-binary", default="build-o2/model_transform_test")
    args = parser.parse_args()

    info = fbi_info(UNIT)
    if not bool(float(info.get("canfly", "0"))):
        raise AssertionError("VERBALL FBI is no longer a flying unit")
    height = 100
    max_speed = int(float(info["maxvelocity"]) * 65536)
    sequence = [
        {"target": (800, height, 800)},
        {"target": (800, height, 800)},
        {"target": (-800, height, 800), "heading": 32768},
        {"target": (-800, height, 800), "heading": 32768},
        {"target": (160, height, 160), "speed": 0},
        {"target": (160, height, 160), "speed": 0},
        {"target": (800, height, -800), "speed": max_speed, "heading": 0},
        {"target": (800, height, -800), "mode": 1, "state_mode": 1},
        {"target": (800, height, -800), "mode": 2, "state_mode": 2},
    ]
    native = native_transition_trace(UNIT, sequence)
    if len(native["snapshots"]) != len(sequence) + 1:
        raise AssertionError(("native tick snapshots", len(native["snapshots"])))

    first_calls = [(name, values) for tick, name, values in native["events"] if tick == 0]
    expected_first = [("TurnDirection", (-135,)), ("MoveRate", (3,)),
                      ("setSFXoccupy", (5,))]
    if first_calls != expected_first:
        raise AssertionError(("native flight movement call-ins", first_calls, expected_first))
    accepted_names = {name for _, name, _ in native["accepted_events"]}
    if not {"TurnDirection", "MoveRate"}.issubset(accepted_names):
        raise AssertionError(("COB callback methods not accepted", accepted_names))

    world = local_trace(args.script_binary, UNIT, COB_PATH, 0,
                        native["events"], len(sequence))
    if len(world) != len(native["snapshots"]):
        raise AssertionError(("native/World snapshot count", len(native["snapshots"]),
                              len(world)))
    if world != native["snapshots"]:
        for tick, (expected, actual) in enumerate(zip(native["snapshots"], world)):
            if expected != actual:
                field = next((index for index, values in enumerate(zip(expected, actual))
                              if values[0] != values[1]), None)
                raise AssertionError(("native/World COB state", tick, field,
                                      expected[field] if field is not None else None,
                                      actual[field] if field is not None else None))
        raise AssertionError("native/World COB state mismatch without a differing field")

    header = struct.unpack_from("<10I", COB_PATH.read_bytes())
    piece_names = names_at(COB_PATH.read_bytes(), header[8], header[2])
    piece_index = {name.lower(): index for index, name in enumerate(piece_names)}
    piece_start = 2 + header[4] + 16 * 41
    words_per_piece_snapshot = 25  # 19 native records + six captured pose words

    def tick_pose(tick):
        row = native["snapshots"][tick]
        pose = [row[piece_start + index * words_per_piece_snapshot + 19:
                    piece_start + index * words_per_piece_snapshot + 25]
                for index in range(header[2])]
        return pose

    before = tick_pose(SAMPLE_TICK - 1)
    pose = tick_pose(SAMPLE_TICK)
    wing_indices = [piece_index[name.lower()] for name in WING_PIECES]
    if not any(any(pose[index]) for index in wing_indices):
        raise AssertionError(("native wing poses were not emitted", SAMPLE_TICK))
    if all(before[index] == pose[index] for index in wing_indices):
        raise AssertionError(("wing poses did not advance across native COB ticks",
                              SAMPLE_TICK - 1, SAMPLE_TICK))

    heading = sequence[SAMPLE_TICK - 1].get("heading", 0) & 0xFFFF
    pieces, vertices, error, worst = compare_geometry(pose, heading, args.model_binary)
    if error >= 0.001:
        raise AssertionError(("native/World model transform mismatch", error, worst))
    print(f"PASS: native VERBALL flight callbacks {first_calls}; native/World COB state "
          f"matches for {len(native['snapshots'])} boundaries; wing poses advance from "
          f"tick {SAMPLE_TICK - 1} to {SAMPLE_TICK}; native 0x4ee620 matches World across "
          f"{pieces} shipped 3DO pieces/{vertices} vertices (max delta {error:.8f} at "
          f"{worst[1]} vertex {worst[2]})")
    print("LIMITS: controlled flight mover inputs and unit services; no pathfinding, "
          "camera/projection, texture, framebuffer, or GUI comparison.")


if __name__ == "__main__":
    main()
