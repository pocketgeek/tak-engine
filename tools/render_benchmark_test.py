#!/usr/bin/env python3
"""Offline checks for tools/render_benchmark.py: log parsing, windows, the PNG
comparer and A/B aggregation. Needs no client, data or GPU."""

import struct
import sys
import tempfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import render_benchmark as rb  # noqa: E402


def log(frames, hash_="00ff00ff00ff00ff", stats=True):
    lines = ['RENDER_ENV video=offscreen renderer=opengl accelerated=1 vsync=0 target=1 '
             'output=1280x960 window=1280x960 world=1075x888 scale=1.000x1.000 max_texture=32768x32768 '
             'gl_vendor="NVIDIA Corporation" gl_renderer="Some GPU/PCIe" gl_version="4.6.0" timer_query=1',
             "render scene: sites=1 ghosts=0 flyers=0 corpses=0 boats=0 scenery=0 center=5000,5000 offset=1,2",
             "shadow benchmark: requested=10 spawned=9 columns=4 zoom=1.000 type=mixed"]
    for f in range(frames):
        lines.append(f"DRAWPHASE frame={f} proj=0.5 submit=1.0 shadow=0.25 atlas=0.0 body=0.5 gpu={f}.5 "
                     "vram=100 body_vertices=10 body_calls=2 images=0 image_bakes=0 bake_vertices=0 "
                     "bake_calls=0 cache_switches=0 image_bytes=0")
        if stats:
            lines.append(f"RENDERSTATS frame={f} gpu_timeline_ms=0.1 t_geometry=0.2 visible_units={f} "
                         "geometry_hits=3 miss_cold=1")
        lines.append(f"FRAME ms={f + 1}.0 update=0.1 anim=0.0 draw=0.5 present=0.25 tick={f} live=9 wall_ms=1")
    lines.append(f"RENDER_STUDY frames={frames} hash={hash_} gpu_textures=1.00MiB")
    return "\n".join(lines) + "\n"


def write_png(path, w, h, pixels):
    raw = b"".join(b"\x00" + bytes(pixels[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    chunk = lambda kind, body: struct.pack(">I", len(body)) + kind + body + struct.pack(
        ">I", zlib.crc32(kind + body) & 0xffffffff)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def check(cond, what):
    if not cond:
        raise SystemExit(f"FAIL: {what}")


def main():
    parsed = rb.parse_log(log(10), 4, 10)
    check(parsed["env"]["renderer"] == "opengl" and parsed["env"]["output"] == "1280x960", "RENDER_ENV fields")
    check(parsed["env"]["gl_renderer"] == "Some GPU/PCIe", "quoted RENDER_ENV value")
    check(parsed["world_hash"] == "00ff00ff00ff00ff" and parsed["frames_done"] == 10, "completion line")
    check(parsed["spawned"] == 9 and parsed["scene_line"].startswith("sites=1"), "scene lines")
    check(len(parsed["frame"]) == 10 and len(parsed["drawphase"]) == 10 and len(parsed["stats"]) == 10, "rows")
    s = rb.summarize(parsed, 4, 10)
    check(s["frame_ms"]["total"]["n"] == 6 and s["frame_ms"]["total"]["mean"] == 7.5, "warmup window positional")
    check(s["drawphase"]["gpu"]["mean"] == 7.0, "drawphase window by frame index")
    check(s["stats"]["visible_units"]["p50"] == 6 and s["stats_last"]["visible_units"] == 9, "stats window")
    seg = rb.summarize(parsed, 2, 5)
    check(seg["frame_ms"]["total"]["n"] == 3 and seg["stats"]["visible_units"]["mean"] == 3, "segment window")
    old = rb.parse_log(log(10, stats=False), 4, 10)
    check("stats" not in rb.summarize(old, 4, 10), "pre-telemetry client parses without RENDERSTATS")
    check(rb.world_rect(parsed["env"], 1280, 960) == (0, 0, 1075, 888), "world rect from RENDER_ENV")
    check(rb.world_rect(None, 1280, 960) == (0, 0, 1280, 960), "world rect fallback")
    check(rb.parse_cpus("16-18,20") == {16, 17, 18, 20}, "cpu list")

    with tempfile.TemporaryDirectory() as tmp:
        w, h = 7, 5
        a = [((x * 37 + y * 11) % 256) for y in range(h) for x in range(w * 4)]
        b = list(a)
        b[(3 * w + 6) * 4 + 1] ^= 0x40          # one pixel inside the world rect...
        b[(0 * w + 2) * 4 + 3] ^= 0x01          # ...and an alpha-only change outside it
        write_png(f"{tmp}/a.png", w, h, a)
        write_png(f"{tmp}/b.png", w, h, b)
        check(rb.read_png(f"{tmp}/a.png")[3] == bytes(a), "PNG round trip")
        full = rb.compare_png(f"{tmp}/a.png", f"{tmp}/b.png")
        check(full["differing"] == 2 and full["max_channel_error"] == 0x40, "full-frame compare")
        part = rb.compare_png(f"{tmp}/a.png", f"{tmp}/b.png", (0, 1, w, h))
        check(part["differing"] == 1 and part["pixels"] == w * (h - 1), "rect compare")
        same = rb.compare_png(f"{tmp}/a.png", f"{tmp}/a.png")
        check(same["differing"] == 0, "identical compare")

    def result(label, total):
        return {"capture": False, "scene": "s", "size": "normal", "label": label,
                "load": {"loadavg_before": 1.0, "legion_scenario_before": 0},
                "measured": {"frame_ms": {"total": {"mean": total, "p95": total, "p99": total},
                                          "draw": {"mean": total}, "present": {"mean": 0.1}},
                             "drawphase": {}, "stats": {"visible_units": {"mean": 5}}}}
    report = rb.aggregate([result("A", 10.0), result("B", 8.0), result("B", 8.2), result("A", 10.4)])
    d = report[0]["delta"]["frame_ms.total.mean"]
    check(abs(d["relative"] - (8.1 - 10.2) / 10.2) < 1e-9 and d["significant"], "A/B delta")
    noisy = rb.aggregate([result("A", 10.0), result("B", 9.9), result("B", 9.95), result("A", 10.3)])
    check(not noisy[0]["delta"]["frame_ms.total.mean"]["significant"], "delta inside A/A noise")
    check("| s | normal | A | 2 | 5 |" in rb.markdown(report), "markdown row")
    for name, scene in rb.SCENES.items():
        env = rb.scene_env(name, "normal")
        check(env["TAK_SHADOW_BENCH"] == str(scene["count"]) and scene.get("notes"), f"scene {name}")
    check(rb.scene_env("total10k-subset", "uhd")["TAK_RENDER_OFFSET"] == "0,3960", "per-size scene override")
    print("PASS: render benchmark parser, windows, PNG compare and aggregation")


if __name__ == "__main__":
    main()
