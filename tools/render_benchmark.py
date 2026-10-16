#!/usr/bin/env python3
"""Matched renderer benchmark matrix for the rendering optimization program.

Runs the debug render study (takclient --testbuild with TAK_RENDER_STUDY) over a
fixed scene matrix and compares one or two optimized-Debug clients:

  A/A (variability):  --a CLIENT [--repeats N]          -> A A A A ...
  A/B (matched):      --a BASE --b CANDIDATE [--repeats N] -> A B B A per group

Each run uses a fresh scratch profile (never the user's settings), a fixed 1/60 s
frame clock, frame-indexed camera paths and a fixed-step local simulation, so the
same scene produces the same world hash on every run. The tool refuses a run
whose backend or output size differs from the request, or whose world hash
differs from the other runs of that scene (a rendering change must not move the
simulation).

Timings: FRAME/DRAWPHASE lines (present in every client since 0.7.23) and the
RENDERSTATS line (phase-0 telemetry; optional so older baselines still parse).
GPU time is the GL_TIME_ELAPSED draw TIMELINE, which includes idle gaps waiting
for CPU submission; it is not device busy time. TAK_PROF_FINISH adds a glFinish
before present so the frame includes completed GPU work (forced-completion
diagnostic timing); pass --pipelined to measure normal pipelined frames instead.

Captures (--capture) are separate runs: readback perturbs timing. Their PNGs are
compared run-to-run over the world view and over the whole frame.

Uses only the Python standard library. Linux-specific: /proc metrics, taskset.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import struct
import subprocess
import sys
import time
import zlib

DEFAULT_MAP = "Ulasem Arena"

# name -> dict(map, count, env, zoom, notes, sizes overrides, segments)
# Per-size overrides: "env@WxH" entries are merged for that output size.
SCENES = {
    "small-battle": dict(count=200, type="araarch", zoom=1.0,
                         env={"TAK_RENDER_BATTLE": "1"},
                         notes="ordinary small battle: 200 Archers, two sides"),
    "stationary-mixed": dict(count=2000, zoom=0.5,
                             notes="dense stationary mixed army (6-type roster)"),
    "moving": dict(count=1200, type="araarch", zoom=0.5, env={"TAK_RENDER_MOTION": "1"},
                   notes="dense continuously moving army (orders reissued every 240 frames)"),
    "total10k-subset": dict(count=10000, zoom=1.0,
                            env={"TAK_RENDER_CENTER": "7600,7600", "TAK_RENDER_OFFSET": "4200,5400"},
                            size_env={"7680x2160": {"TAK_RENDER_OFFSET": "0,3960"}},
                            notes="10k units, camera on a small corner of the army"),
    "total10k-visible": dict(count=10000, zoom=0.25,
                             notes="10k units, (nearly) all on screen"),
    "forest": dict(map="Sea Dragon Spine", count=300, zoom=1.0, env={"TAK_RENDER_FOREST": "1"},
                   notes="densest scenery region of Sea Dragon Spine + 300 mixed units"),
    "pan": dict(count=2000, zoom=0.75, env={"TAK_RENDER_CAMERA": "1"},
                notes="sinusoidal pan (continuous motion with reversals)"),
    "zoom": dict(count=2000, zoom=1.0, env={"TAK_RENDER_CAMERA": "1", "TAK_RENDER_ZOOM": "1"},
                 notes="pan + continuous zoom 0.25..1.0"),
    "jump": dict(count=2000, zoom=0.75, env={"TAK_RENDER_JUMP": "60"},
                 notes="camera jumps between four distant views every 60 frames"),
    "effects": dict(count=600, zoom=0.75, env={"TAK_RENDER_BATTLE": "1", "TAK_RENDER_EXTRAS": "1"},
                    notes="battle + construction sites, ghosts, flyers, corpses, projectiles"),
    "naval": dict(map="Athri Cay", count=1, zoom=0.75, env={"TAK_RENDER_NAVAL": "150"},
                  notes="150 floating hulls on the water nearest the centre of Athri Cay"),
    "churn": dict(count=2000, zoom=0.75,
                  env={"TAK_RENDER_MOTION": "1", "TAK_RENDER_CAMERA": "1",
                       "TAK_RENDER_CHURN": "60", "TAK_RENDER_RESET": "300"},
                  notes="moving army + panning camera; 5% of the army dies/respawns every "
                        "60 frames; simulated render-target reset every 300 frames"),
    "battle-sparse": dict(count=2000, zoom=0.5,
                          env={"TAK_RENDER_BATTLE": "1", "TAK_RENDER_SPARSE_AFTER": "450",
                               "TAK_RENDER_SPARSE_OFFSET": "8800,200"},
                          segments={"battle": (300, 450), "sparse": (450, None)},
                          notes="large battle, then from frame 450 a sustained empty view"),
    "constrained": dict(count=2000, zoom=0.5, settings={"gpuMemoryLimit": "512"},
                        notes="stationary-mixed under the 512 MiB GPU MEMORY LIMIT"),
}
QUICK = ["small-battle", "stationary-mixed", "moving", "pan"]
SIZES = {"normal": (1280, 960), "uhd": (7680, 2160)}

FRAME_RE = re.compile(
    r"^FRAME ms=([\d.]+) update=([\d.]+) anim=([\d.]+) draw=([\d.]+) present=([\d.]+)"
    r".*tick=(\d+) live=(\d+)", re.M)
KV_RE = re.compile(r"(\w+)=(-?[\d.]+)")
ENV_RE = re.compile(r'(\w+)=("[^"]*"|\S+)')


def distribution(values):
    if not values:
        return None
    ordered = sorted(values)
    pick = lambda p: ordered[min(len(ordered) - 1, math.floor((len(ordered) - 1) * p / 100))]
    return {"mean": statistics.fmean(values), "p50": pick(50), "p95": pick(95), "p99": pick(99),
            "max": ordered[-1], "n": len(values)}


def parse_kv(line):
    return {k: float(v) for k, v in KV_RE.findall(line)}


def parse_env(line):
    return {k: v.strip('"') for k, v in ENV_RE.findall(line[len("RENDER_ENV "):])}


def parse_log(text, warmup, frames):
    """Parse one run's stdout+stderr. Returns per-frame rows (after warmup)."""
    env_lines = [l for l in text.splitlines() if l.startswith("RENDER_ENV ")]
    done = re.search(r"RENDER_STUDY frames=(\d+) hash=([a-f0-9]+)", text)
    frame_rows = [[float(v) for v in m] for m in FRAME_RE.findall(text)]
    phase_rows, stat_rows = [], []
    for line in text.splitlines():
        if line.startswith("DRAWPHASE "):
            phase_rows.append(parse_kv(line))
        elif line.startswith("RENDERSTATS "):
            stat_rows.append(parse_kv(line))
    scene = re.search(r"render scene: (.*)", text)
    spawn = re.search(r"shadow benchmark: requested=(\d+) spawned=(\d+)", text)
    return {
        "env": parse_env(env_lines[0]) if env_lines else None,
        "frames_done": int(done[1]) if done else None,
        "world_hash": done[2] if done else None,
        "frame": frame_rows, "drawphase": phase_rows, "stats": stat_rows,
        "scene_line": scene[1] if scene else None,
        "spawned": int(spawn[2]) if spawn else None,
        "warmup": warmup, "frames": frames,
    }


def window(rows, begin, end, key="frame"):
    """Rows whose frame index lies in [begin, end). FRAME rows are positional."""
    if key is None:
        return rows[begin:end]
    return [r for r in rows if r.get(key, -1) >= begin and (end is None or r[key] < end)]


def summarize(parsed, begin, end):
    frames = parsed["frame"][begin:end]
    out = {"frame_ms": {k: distribution([r[i] for r in frames]) for i, k in
                        enumerate(("total", "update", "anim", "draw", "present"))}}
    phases = window(parsed["drawphase"], begin, end)
    if phases:
        out["drawphase"] = {k: distribution([r[k] for r in phases]) for k in phases[0] if k != "frame"}
    stats = window(parsed["stats"], begin, end)
    if stats:
        out["stats"] = {k: distribution([r[k] for r in stats]) for k in stats[0] if k != "frame"}
        last = stats[-1]
        out["stats_last"] = {k: v for k, v in last.items() if k != "frame"}
    return out


def loadavg():
    try:
        return float(Path("/proc/loadavg").read_text().split()[0])
    except OSError:
        return None


def count_processes(name):
    n = 0
    for p in Path("/proc").iterdir():
        if p.name.isdigit():
            try:
                if (p / "comm").read_text().strip() == name[:15]:
                    n += 1
            except OSError:
                pass
    return n


def cpu_busy(cpus):
    """Cumulative (busy, total) jiffies for the given CPU ids from /proc/stat."""
    busy = total = 0
    try:
        for line in Path("/proc/stat").read_text().splitlines():
            m = re.match(r"cpu(\d+) (.*)", line)
            if m and int(m[1]) in cpus:
                v = [int(x) for x in m[2].split()]
                idle = v[3] + (v[4] if len(v) > 4 else 0)
                busy += sum(v) - idle
                total += sum(v)
    except OSError:
        return None
    return busy, total


def parse_cpus(spec):
    out = set()
    for part in spec.split(","):
        if "-" in part:
            a, b = part.split("-")
            out.update(range(int(a), int(b) + 1))
        elif part:
            out.add(int(part))
    return out


def process_sample(pid):
    try:
        status = Path(f"/proc/{pid}/status").read_text()
        rss = int(re.search(r"VmRSS:\s+(\d+)", status)[1]) * 1024
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return time.monotonic(), rss, int(fields[11]) + int(fields[12])
    except (OSError, TypeError, IndexError):
        return None


def scene_env(scene, size_key):
    s = SCENES[scene]
    env = {"TAK_SHADOW_BENCH": str(s["count"]), "TAK_PROFILE_ZOOM": str(s["zoom"])}
    if s.get("type"):
        env["TAK_SHADOW_BENCH_TYPE"] = s["type"]
    env.update(s.get("env", {}))
    w, h = SIZES[size_key]
    env.update(s.get("size_env", {}).get(f"{w}x{h}", {}))
    return env


SETTINGS = {
    "fullscreen": "0", "vsync": "0", "maxFps": "0", "uiScale": "1", "scorecardScale": "1",
    "treeSway": "1", "unitShadows": "1", "healthBars": "1", "statsPanel": "0",
    "hardwareCursor": "1", "smoothMotion": "1", "edgeScroll": "0", "masterVol": "0",
}


def wait_quiet(args, cpus):
    """Wait (up to --wait-quiet seconds) until the pinned CPUs are mostly idle,
    sampling 3 s at a time. Returns (seconds waited, last busy fraction)."""
    if not args.wait_quiet or not cpus:
        return 0.0, None
    start, busy = time.monotonic(), None
    while True:
        a = cpu_busy(cpus)
        time.sleep(3)
        b = cpu_busy(cpus)
        busy = (b[0] - a[0]) / max(1, b[1] - a[1]) if a and b else 0.0
        if busy <= args.quiet_busy or time.monotonic() - start >= args.wait_quiet:
            return time.monotonic() - start, busy


def run_one(args, client, label, scene, size_key, leg, capture=False):
    w, h = SIZES[size_key]
    name = f"{scene}-{size_key}-{leg:02d}-{label}" + ("-capture" if capture else "")
    root = args.output / name
    if root.exists():
        shutil.rmtree(root)
    profile = root / "profile"
    pref = profile / "TAKengine" / "TAKingdoms"
    pref.mkdir(parents=True, exist_ok=True)
    settings = dict(SETTINGS, **SCENES[scene].get("settings", {}))
    (pref / "settings.ini").write_text("".join(f"{k} = {v}\n" for k, v in settings.items()))
    env = {k: v for k, v in os.environ.items() if not k.startswith("TAK_") and k != "DISPLAY"}
    env.update(SDL_VIDEODRIVER=args.video_driver, SDL_RENDER_DRIVER=args.render_driver,
               SDL_AUDIODRIVER="dummy", XDG_CONFIG_HOME=str(profile), XDG_DATA_HOME=str(profile),
               HOME=str(profile), TAK_PROF="1", TAK_PROF_FRAMES="1", TAK_RENDER_STUDY="1",
               TAK_RENDER_FRAMES=str(args.frames))
    if not args.pipelined:
        env["TAK_PROF_FINISH"] = "1"
    env.update(scene_env(scene, size_key))
    for extra in args.env:
        k, _, v = extra.partition("=")
        env[k] = v
    if capture:
        (root / "captures").mkdir(parents=True, exist_ok=True)
        env["TAK_RENDER_CAPTURE_DIR"] = str(root / "captures")
    command = [str(client), "game", SCENES[scene].get("map", DEFAULT_MAP), "--data", str(args.data),
               "--testbuild", "--winsize", str(w), str(h), "--novsync", "--maxfps", "0"]
    if args.cpus:
        command = ["taskset", "-c", args.cpus] + command
    cpus = parse_cpus(args.cpus) if args.cpus else set()
    waited, idle_busy = wait_quiet(args, cpus) if not capture else (0.0, None)
    load = {"waited_quiet_s": waited, "busy_before": idle_busy, "loadavg_before": loadavg(), "legion_scenario_before": count_processes("legion_scenario"),
            "pinned_cpus": args.cpus}
    busy0 = cpu_busy(cpus) if cpus else None
    log, err = root / "stdout.log", root / "stderr.log"
    samples, start = [], time.monotonic()
    with log.open("w") as out, err.open("w") as errors:
        process = subprocess.Popen(command, env=env, stdout=out, stderr=errors)
        try:
            while process.poll() is None:
                s = process_sample(process.pid)
                if s:
                    samples.append(s)
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
    busy1 = cpu_busy(cpus) if cpus else None
    load.update(loadavg_after=loadavg(), legion_scenario_after=count_processes("legion_scenario"))
    if busy0 and busy1 and busy1[1] > busy0[1]:
        load["pinned_cpu_busy_fraction"] = (busy1[0] - busy0[0]) / (busy1[1] - busy0[1])
    if process.returncode:
        raise RuntimeError(f"{name}: exit {process.returncode}; see {log}")
    parsed = parse_log(log.read_text() + "\n" + err.read_text(), args.warmup, args.frames)
    if parsed["frames_done"] != args.frames:
        raise RuntimeError(f"{name}: requires a Debug client with render-study support")
    renv = parsed["env"]
    if renv is None:
        if not args.allow_unverified_env:
            raise RuntimeError(f"{name}: client prints no RENDER_ENV (pass --allow-unverified-env "
                               "for a pre-phase-0 client)")
    else:
        if renv.get("renderer") != args.render_driver:
            raise RuntimeError(f"{name}: asked for renderer {args.render_driver}, got {renv.get('renderer')}")
        if renv.get("output") != f"{w}x{h}":
            raise RuntimeError(f"{name}: asked for output {w}x{h}, got {renv.get('output')}")
    if len(parsed["frame"]) != args.frames or len(parsed["drawphase"]) != args.frames:
        raise RuntimeError(f"{name}: incomplete measurement records")
    result = {
        "name": name, "scene": scene, "size": size_key, "label": label, "leg": leg,
        "client": str(client), "capture": capture, "world_hash": parsed["world_hash"],
        "render_env": renv, "spawned": parsed["spawned"], "scene_line": parsed["scene_line"],
        "settings": settings, "scene_env": scene_env(scene, size_key),
        "wall_seconds": time.monotonic() - start, "load": load,
        "peak_rss_bytes": max((s[1] for s in samples), default=None),
        "final_rss_bytes": samples[-1][1] if samples else None,
        "measured": summarize(parsed, args.warmup, args.frames),
    }
    segments = SCENES[scene].get("segments")
    if segments:
        result["segments"] = {k: summarize(parsed, b, e if e is not None else args.frames)
                              for k, (b, e) in segments.items()}
    if len(samples) > 1:
        ticks = samples[-1][2] - samples[0][2]
        result["client_cpu_percent_all_cores"] = ticks / os.sysconf("SC_CLK_TCK") / (
            samples[-1][0] - samples[0][0]) * 100 / (os.cpu_count() or 1)
    if capture:
        result["captures"] = sorted(p.name for p in (root / "captures").glob("*.png"))
    else:
        shutil.rmtree(profile, ignore_errors=True)
    (root / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    m = result["measured"]
    print(f"{name}: total mean {m['frame_ms']['total']['mean']:.3f} p95 {m['frame_ms']['total']['p95']:.3f}"
          f" draw {m['frame_ms']['draw']['mean']:.3f} hash {parsed['world_hash']}"
          f" load {load['loadavg_before']}", flush=True)
    return result


# ---- PNG comparison (8-bit RGB/RGBA, non-interlaced: what tak::png writes) ----

def read_png(path):
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, w = 8, [], None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(f"{path}: unsupported PNG format")
            bpp = 4 if ctype == 6 else 3
        elif kind == b"IDAT":
            idat.append(body)
        pos += 12 + length
    raw = zlib.decompress(b"".join(idat))
    stride = w * bpp
    out, prev = bytearray(), bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line
        prev = line
    return w, h, bpp, bytes(out)


def compare_png(a, b, rect=None):
    wa, ha, bpp, pa = read_png(a)
    wb, hb, bppb, pb = read_png(b)
    if (wa, ha, bpp) != (wb, hb, bppb):
        return {"comparable": False}
    x0, y0, x1, y1 = rect if rect else (0, 0, wa, ha)
    differing, max_err = 0, 0
    for y in range(y0, y1):
        row = y * wa * bpp
        sa, sb = pa[row + x0 * bpp:row + x1 * bpp], pb[row + x0 * bpp:row + x1 * bpp]
        if sa == sb:
            continue
        for x in range(0, len(sa), bpp):
            if sa[x:x + bpp] != sb[x:x + bpp]:
                differing += 1
                max_err = max(max_err, max(abs(sa[x + k] - sb[x + k]) for k in range(min(bpp, 3))))
    return {"comparable": True, "pixels": (x1 - x0) * (y1 - y0), "differing": differing,
            "max_channel_error": max_err}


def world_rect(render_env, w, h):
    # The world view the client reports (left of the command panel, above the
    # bottom bar). Excludes the HUD and its dynamic text.
    m = re.match(r"(\d+)x(\d+)$", (render_env or {}).get("world", ""))
    return (0, 0, int(m[1]), int(m[2])) if m else (0, 0, w, h)


# ---- Aggregation ----

KEY_METRICS = [("frame_ms", "total", "mean"), ("frame_ms", "total", "p95"), ("frame_ms", "total", "p99"),
               ("frame_ms", "draw", "mean"), ("frame_ms", "present", "mean"),
               ("drawphase", "proj", "mean"), ("drawphase", "shadow", "mean"),
               ("drawphase", "body", "mean"), ("drawphase", "gpu", "mean")]


def metric(result, path):
    node = result["measured"]
    for k in path:
        if node is None or k not in node:
            return None
        node = node[k]
    return node


# The pinned CPUs' busy fraction includes the client itself (about 0.2 of
# eight CPUs for these scenes). Above this, another process was competing for
# the pinned cores and the run's timing is not comparable.
CONTENDED_BUSY = 0.5


def contended(result):
    return (result["load"].get("pinned_cpu_busy_fraction") or 0) > CONTENDED_BUSY


def aggregate(results, exclude_contended=False):
    groups = {}
    for r in results:
        if r["capture"] or (exclude_contended and contended(r)):
            continue
        groups.setdefault((r["scene"], r["size"]), {}).setdefault(r["label"], []).append(r)
    report = []
    for (scene, size), by_label in sorted(groups.items()):
        row = {"scene": scene, "size": size, "labels": {}}
        for label, runs in by_label.items():
            entry = {"runs": len(runs)}
            for path in KEY_METRICS:
                vals = [v for v in (metric(r, path) for r in runs) if v is not None]
                if not vals:
                    continue
                mean = statistics.fmean(vals)
                spread = (max(vals) - min(vals)) / mean if mean > 0 and len(vals) > 1 else None
                entry[".".join(path)] = {"mean": mean, "min": min(vals), "max": max(vals),
                                         "aa_rel_spread": spread}
            st = [r["measured"].get("stats") for r in runs if r["measured"].get("stats")]
            if st:
                entry["visible_units"] = st[0].get("visible_units", {}).get("mean")
                entry["painter_items"] = st[0].get("painter_items", {}).get("mean")
            entry["loadavg"] = [r["load"]["loadavg_before"] for r in runs]
            entry["legion_scenario"] = [r["load"]["legion_scenario_before"] for r in runs]
            entry["contended_runs"] = sum(contended(r) for r in runs)
            row["labels"][label] = entry
        if len(by_label) == 2 and "A" in by_label and "B" in by_label:
            row["delta"] = {}
            for path in KEY_METRICS:
                k = ".".join(path)
                a, b = row["labels"]["A"].get(k), row["labels"]["B"].get(k)
                if a and b and a["mean"] > 0:
                    noise = max(a["aa_rel_spread"] or 0, b["aa_rel_spread"] or 0)
                    change = (b["mean"] - a["mean"]) / a["mean"]
                    row["delta"][k] = {"relative": change, "noise": noise,
                                       "significant": abs(change) > max(2 * noise, 0.02)}
        report.append(row)
    return report


def markdown(report):
    lines = ["| scene | size | label | runs (contended) | visible | total mean | p95 | p99 | draw | present | gpu timeline | A/A spread (total mean) | A/A spread (draw mean) |",
             "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    fmt = lambda e, k: f"{e[k]['mean']:.3f}" if k in e else "-"
    for row in report:
        for label, e in sorted(row["labels"].items()):
            spread = e.get("frame_ms.total.mean", {}).get("aa_rel_spread")
            dspread = e.get("frame_ms.draw.mean", {}).get("aa_rel_spread")
            visible = e.get("visible_units")
            lines.append(f"| {row['scene']} | {row['size']} | {label} | {e['runs']} ({e.get('contended_runs', 0)}) | "
                         f"{'-' if visible is None else f'{visible:.0f}'} | {fmt(e, 'frame_ms.total.mean')} | "
                         f"{fmt(e, 'frame_ms.total.p95')} | {fmt(e, 'frame_ms.total.p99')} | "
                         f"{fmt(e, 'frame_ms.draw.mean')} | {fmt(e, 'frame_ms.present.mean')} | "
                         f"{fmt(e, 'drawphase.gpu.mean')} | "
                         f"{'-' if spread is None else f'{spread * 100:.1f}%'} | "
                         f"{'-' if dspread is None else f'{dspread * 100:.1f}%'} |")
    deltas = [row for row in report if row.get("delta")]
    if deltas:
        lines += ["", "| scene | size | metric | B vs A | noise (max A/A spread) | material |",
                  "|---|---|---|---:|---:|---|"]
        for row in deltas:
            for k, d in row["delta"].items():
                lines.append(f"| {row['scene']} | {row['size']} | {k} | {d['relative'] * 100:+.1f}% | "
                             f"{d['noise'] * 100:.1f}% | {'yes' if d['significant'] else 'no'} |")
    return "\n".join(lines) + "\n"


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--a", type=Path, required=True, help="baseline optimized-Debug takclient")
    p.add_argument("--b", type=Path, help="candidate takclient (omit for A/A)")
    p.add_argument("--data", type=Path, required=True, help="legally owned retail install")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--scenes", nargs="+", default=QUICK, help=f"scene names, 'all' or 'quick'; known: {', '.join(SCENES)}")
    p.add_argument("--sizes", nargs="+", default=["normal"], choices=list(SIZES))
    p.add_argument("--repeats", type=int, default=2, help="A/B: ABBA groups; A/A: runs = 2*repeats")
    p.add_argument("--frames", type=int, default=900)
    p.add_argument("--warmup", type=int, default=300)
    p.add_argument("--timeout", type=float, default=600)
    p.add_argument("--cpus", default="16-23", help="taskset CPU list for the client ('' = unpinned)")
    p.add_argument("--wait-quiet", type=float, default=0,
                   help="before each run, wait up to this many seconds for the pinned CPUs to go idle")
    p.add_argument("--quiet-busy", type=float, default=0.15, help="idle threshold for --wait-quiet")
    p.add_argument("--video-driver", default="offscreen")
    p.add_argument("--render-driver", default="opengl")
    p.add_argument("--pipelined", action="store_true", help="no glFinish before present")
    p.add_argument("--capture", action="store_true", help="also one capture run per label per scene and compare")
    p.add_argument("--capture-only", action="store_true")
    p.add_argument("--env", action="append", default=[], help="extra KEY=VALUE for every run")
    p.add_argument("--allow-unverified-env", action="store_true")
    p.add_argument("--list", action="store_true", help="print the scene matrix and exit")
    p.add_argument("--reaggregate", action="store_true",
                   help="rebuild report.json/report.md from --output/results.json without running")
    p.add_argument("--exclude-contended", action="store_true",
                   help=f"leave runs whose pinned CPUs were over {int(CONTENDED_BUSY * 100)}%% busy out of the report")
    args = p.parse_args(argv)
    if args.list:
        for name, s in SCENES.items():
            print(f"{name:18s} {s.get('map', DEFAULT_MAP):18s} {s['notes']}")
        return 0
    scenes = list(SCENES) if args.scenes == ["all"] else QUICK if args.scenes == ["quick"] else args.scenes
    for s in scenes:
        if s not in SCENES:
            p.error(f"unknown scene {s}")
    if not 0 <= args.warmup < args.frames <= 10000:
        p.error("require 0 <= warmup < frames <= 10000")
    args.output = args.output.resolve()
    if args.reaggregate:
        report = aggregate(json.loads((args.output / "results.json").read_text()), args.exclude_contended)
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        (args.output / "report.md").write_text(markdown(report))
        print(markdown(report))
        return 0
    args.output.mkdir(parents=True, exist_ok=True)
    clients = {"A": args.a.resolve()}
    if args.b:
        clients["B"] = args.b.resolve()
    metadata = {
        "clients": {k: {"path": str(v), "sha256": hashlib.sha256(v.read_bytes()).hexdigest()}
                    for k, v in clients.items()},
        "options": {k: str(v) for k, v in vars(args).items()}, "scenes": {s: SCENES[s] for s in scenes},
        "settings": SETTINGS, "logical_cpus": os.cpu_count(), "uname": " ".join(os.uname()),
        "gpu_measurement": "GL_TIME_ELAPSED draw timeline; includes CPU-submission gaps; not busy time",
        "frame_measurement": "pipelined" if args.pipelined else "forced completion (glFinish before present)",
    }
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2, default=str) + "\n")
    order = (["A", "B", "B", "A"] if "B" in clients else ["A", "A"]) * args.repeats
    results = []
    resultfile = args.output / "results.json"
    for size in args.sizes:
        for scene in scenes:
            hashes = set()
            if not args.capture_only:
                for leg, label in enumerate(order):
                    r = run_one(args, clients[label], label, scene, size, leg)
                    hashes.add(r["world_hash"])
                    results.append(r)
                    resultfile.write_text(json.dumps(results, indent=2) + "\n")
            if args.capture or args.capture_only:
                caps, cap_env = {}, None
                labels = list(clients) + (["A"] if "B" not in clients else [])
                for i, label in enumerate(labels):
                    r = run_one(args, clients[label], label, scene, size, 90 + i, capture=True)
                    hashes.add(r["world_hash"])
                    caps[f"{label}{i}"] = args.output / r["name"] / "captures"
                    cap_env = cap_env or r["render_env"]
                    results.append(r)
                keys = list(caps)
                w, h = SIZES[size]
                comparison = {}
                for png in sorted(p.name for p in caps[keys[0]].glob("*.png")):
                    a, b = caps[keys[0]] / png, caps[keys[1]] / png
                    if b.exists():
                        comparison[png] = {"world": compare_png(a, b, world_rect(cap_env, w, h)),
                                           "full": compare_png(a, b)}
                (args.output / f"captures-{scene}-{size}.json").write_text(
                    json.dumps({"pair": keys, "comparison": comparison}, indent=2) + "\n")
                worst = max((c["world"].get("differing", 0) for c in comparison.values()), default=0)
                print(f"captures {scene}/{size} {keys[0]} vs {keys[1]}: {len(comparison)} frames, "
                      f"worst world-view differing pixels {worst}", flush=True)
            if len(hashes) > 1:
                raise RuntimeError(f"{scene}/{size}: world hashes differ between runs: {hashes}")
            resultfile.write_text(json.dumps(results, indent=2) + "\n")
    report = aggregate(results, args.exclude_contended)
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    (args.output / "report.md").write_text(markdown(report))
    print(markdown(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
