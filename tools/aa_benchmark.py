#!/usr/bin/env python3
"""Fixed-frame independent-AA measurements using an optimized Debug takclient.

Run without --capture for timings and repeat with --capture for visual review.
The default covers all 15 combinations; --reference runs paired ABBA comparisons.
Only the Python standard library is required. Logs and retail captures stay in
the caller's output directory, outside Git. Uses the existing render-study hooks.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

from render_reuse_benchmark import SCENES as REUSE_SCENES, distribution, process_sample


SCENES = {
    name: {"map": "Ulasem Arena", "environment": {
        "TAK_SHADOW_BENCH": str(count), "TAK_PROFILE_ZOOM": str(zoom),
        **({"TAK_SHADOW_BENCH_TYPE": kind} if kind else {}), **extra}}
    for name, (count, kind, zoom, extra) in REUSE_SCENES.items()
}
SCENES.update({
    "construction": {"map": "Ulasem Arena", "environment": {
        "TAK_CONJURE_TEST": "1", "TAK_CONJURE_BUILDER": "zonhunt",
        "TAK_CONJURE_TARGET": "zonter"}},
    "projectiles": {"map": "Ulasem Arena", "arguments": ["--firetest", "--nofog"],
                    "environment": {"TAK_PROJECTILE_TEST": "verbal"}},
    # This fixture searches for actual deep water before spawning the transport.
    "water": {"map": "Varro Passage", "arguments": ["--firetest", "--nofog"],
              "environment": {"TAK_POINT_TEST": "1"}},
})


def pair(text):
    try:
        terrain, model = map(int, text.split(":"))
        if terrain not in (0, 2, 4) or model not in (0, 2, 4, 8, 16):
            raise ValueError()
        return terrain, model
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected terrain:model (0/2/4 : 0/2/4/8/16)") from error


def run(args, scene, samples, label, client, leg):
    terrain, model = samples
    name = f"{scene}-t{terrain}-m{model}-{leg}-{label}"
    directory = args.output / name
    profile = directory / "profile"
    preferences = profile / "TAKengine" / "TAKingdoms"
    preferences.mkdir(parents=True, exist_ok=True)
    (preferences / "settings.ini").write_text(
        "fullscreen = 0\nvsync = 0\nmaxFps = 60\nuiScale = 1\n"
        "scorecardScale = 1\ntreeSway = 1\nunitShadows = 1\n"
        # Hide wall-clock/CPU text so captures compare fixed world and UI state.
        "healthBars = 1\nstatsPanel = 0\nhardwareCursor = 1\n"
        "smoothMotion = 1\nedgeScroll = 0\nmasterVol = 0\n"
        "zoomSmoothing = off\nsmoothArt = 0\nvideoDeblock = 0\n"
        f"zoomedOutTerrain = {terrain or 'off'}\nunitEdgeAA = {model}\n"
    )
    env = {key: value for key, value in os.environ.items() if not key.startswith("TAK_")}
    env.update(
        SDL_VIDEODRIVER=args.video_driver, SDL_RENDER_DRIVER=args.render_driver,
        SDL_AUDIODRIVER="dummy", XDG_CONFIG_HOME=str(profile), XDG_DATA_HOME=str(profile),
        TAK_PROF="1", TAK_PROF_FRAMES="1", TAK_DISTANT_STATS="1", TAK_PROF_FINISH="1",
        TAK_RENDER_STUDY="1", TAK_RENDER_FRAMES=str(args.frames),
        TAK_AA_TILE_REFERENCE="1" if args.tile_reference and label=="reference" else "0",
        **SCENES[scene]["environment"],
    )
    if args.capture:
        captures = directory / "captures"
        captures.mkdir(exist_ok=True)
        env["TAK_RENDER_CAPTURE_DIR"] = str(captures)
    command = [str(client), "game", SCENES[scene]["map"], "--data", str(args.data),
               "--testbuild", "--winsize", str(args.width), str(args.height),
               "--novsync", "--maxfps", "0", *SCENES[scene].get("arguments", [])]
    stdout, stderr = directory / "stdout.log", directory / "stderr.log"
    samples_cpu = []
    start = time.monotonic()
    with stdout.open("w") as output, stderr.open("w") as errors:
        process = subprocess.Popen(command, env=env, stdout=output, stderr=errors)
        try:
            while process.poll() is None:
                sample = process_sample(process.pid)
                if sample:
                    samples_cpu.append(sample)
                if time.monotonic() - start > args.timeout:
                    raise TimeoutError(f"{name}: exceeded {args.timeout}s")
                time.sleep(0.1)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    if process.returncode:
        raise RuntimeError(f"{name}: exit {process.returncode}; see {directory}")
    text = stdout.read_text() + "\n" + stderr.read_text()
    completion = re.search(r"RENDER_STUDY frames=(\d+) hash=([a-f0-9]+)", text)
    if not completion or int(completion[1]) != args.frames:
        raise RuntimeError(f"{name}: needs a Debug client with render-study hooks")
    aa = {kind: {"requested": int(requested), "effective": int(effective), "target_mib": float(mib)}
          for kind, requested, effective, mib in re.findall(
              r"AA (terrain|models): requested (\d+)x, effective (\d+)x, targets ([\d.]+) MiB", text)}
    if len(aa) != 2 or aa["terrain"]["requested"] != terrain or aa["models"]["requested"] != model:
        raise RuntimeError(f"{name}: requested AA settings were not confirmed")
    if not args.allow_fallback and (aa["terrain"]["effective"] != terrain or aa["models"]["effective"] != model):
        raise RuntimeError(f"{name}: AA fell back; use --allow-fallback to measure fallback explicitly")
    frames = re.findall(
        r"^FRAME ms=([\d.]+) update=([\d.]+) anim=([\d.]+) draw=([\d.]+) "
        r"present=([\d.]+).*tick=(\d+) live=(\d+)", text, re.M)
    frames = [[float(value) for value in row] for row in frames][args.warmup:]
    phases = []
    for line in text.splitlines():
        if line.startswith("DRAWPHASE "):
            row = {key: float(value) for key, value in re.findall(r"(\w+)=(-?[\d.]+)", line)}
            if row["frame"] >= args.warmup:
                phases.append(row)
    if len(frames) != args.frames - args.warmup or len(phases) != len(frames):
        raise RuntimeError(f"{name}: incomplete measurement records")
    result = {
        "scene": scene, "label": label, "leg": leg, "frames": len(frames),
        "world_hash": completion[2], "wall_seconds": time.monotonic() - start,
        "captures_enabled": args.capture, "aa": aa,
        "peak_rss_bytes": max((sample[1] for sample in samples_cpu), default=None),
        "frame_ms": {key: distribution([row[i] for row in frames]) for i, key in
                     enumerate(("total", "update", "animation", "draw", "present"))},
        "render_work": {key: distribution([row[key] for row in phases])
                        for key in phases[0] if key != "frame"},
    }
    if len(samples_cpu) > 1:
        ticks = samples_cpu[-1][2] - samples_cpu[0][2]
        result["client_cpu_percent_all_cores"] = ticks / os.sysconf("SC_CLK_TCK") / (
            samples_cpu[-1][0] - samples_cpu[0][0]) * 100 / (os.cpu_count() or 1)
    (directory / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"{name}: {result['frame_ms']['total']}; hash {completion[2]}", flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--reference", type=Path, help="saved same-build-config Debug executable")
    parser.add_argument("--tile-reference", action="store_true",
                        help="compare the same client with former per-triangle AA tile scanning")
    parser.add_argument("--label", default="current", choices=("current", "before", "after"))
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenes", nargs="+", choices=SCENES, default=["sparse", "animated"])
    parser.add_argument("--combinations", nargs="+", type=pair,
                        default=[(terrain, model) for terrain in (0, 2, 4) for model in (0, 2, 4, 8, 16)])
    parser.add_argument("--frames", type=int, default=900)
    parser.add_argument("--warmup", type=int, default=300)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=960)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--video-driver", default="offscreen")
    parser.add_argument("--render-driver", default="opengl")
    parser.add_argument("--capture", action="store_true", help="separate visual run; timings include readback")
    parser.add_argument("--allow-fallback", action="store_true", help="record effective AA if requested AA cannot run")
    parser.add_argument("--pair", action="store_true", help="reference/current instead of ABBA")
    args = parser.parse_args()
    if not 0 <= args.warmup < args.frames <= 10000:
        parser.error("require 0 <= warmup < frames <= 10000")
    if args.reference and args.tile_reference:
        parser.error("same-client reference switches cannot be combined with --reference")
    if not 1 <= args.width <= 16384 or not 1 <= args.height <= 16384:
        parser.error("window dimensions must be in 1..16384")
    for field in ("client", "reference", "data", "output"):
        value = getattr(args, field)
        if value is not None:
            setattr(args, field, value.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    metadata = {
        "command_options": vars(args), "logical_cpus": os.cpu_count(), "scenes": SCENES,
        "client_sha256": hashlib.sha256(args.client.read_bytes()).hexdigest(),
        "reference_sha256": hashlib.sha256(args.reference.read_bytes()).hexdigest() if args.reference else None,
        "gpu_measurement": "GL_TIME_ELAPSED draw timeline; may include CPU submission gaps",
        "work_measurement": "SDL body submissions/cache work; not whole-frame hardware draw calls",
        "rss_measurement": "whole-process peak, including assets/simulation/cache; not AA-only",
    }
    (args.output / "metadata.json").write_text(json.dumps(metadata, default=str, indent=2) + "\n")
    results = []
    for scene in args.scenes:
        hashes = set()
        for samples in args.combinations:
            legs = [(args.label, args.client)]
            if args.reference or args.tile_reference:
                legs = [("reference", args.reference or args.client), ("current", args.client)]
                if not args.pair:
                    legs += list(reversed(legs))
            for leg, (label, client) in enumerate(legs):
                result = run(args, scene, samples, label, client, leg)
                hashes.add(result["world_hash"])
                if len(hashes) != 1:
                    raise RuntimeError(f"{scene}: simulation hashes differ between runs/AA settings")
                results.append(result)
                (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
