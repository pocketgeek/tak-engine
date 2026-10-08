#!/usr/bin/env python3
"""Legion hash-identity harness (I2): helpers, crowdbench runner and report.

tools/legion_identity.sh is the driver; it calls this file for three jobs:

  patch-replay SRC DST PROTOCOL
      Copy a .takrep and retarget it at another simulation build: the u32
      protocol at byte offset 8 becomes PROTOCOL, and for PROTOCOL >= 239 the
      empty TOV1 override-pack digest is replaced by the empty TOV2 digest (the
      239 build refuses the TOV1 one; both packs are empty, so the replayed
      world is the same).

  crowdbench --out DIR --base BIN --cand BIN --cores LIST [--quick] [--verify]
      Run the crowdbench identity matrix on both binaries in one job pool
      (one job per core in LIST, every job pinned to LIST) and write
      DIR/crowdbench/{base,cand}.jsonl. The matrix is MATRIX_FULL or, with
      --quick, MATRIX_QUICK (both documented below).

  report --out DIR [--quick] [--verify]
      Read everything the driver left in DIR, print one row per check
      (base value, candidate value, SAME/DIFF/FAIL/SKIP), write
      DIR/identity.txt (every row) and DIR/identity.json, and exit 0 only when
      no row is DIFF or FAIL. SKIP (a check a build cannot run, e.g. replays
      on a Release takclient, which reads no TAK_* variables) does not fail.

Hashes are compared, never timings: everything here is deterministic.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import struct
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

ROLES = ("base", "cand")
TICKS = 6000
MOVING = 100
SEEDS = (0, 1, 2)
MODES = ("retail", "legion")
EXECUTIONS = ("serial", "workers")

# Full matrix: the T8 nightly-screen shape (every scenario at 200x1 and 500x4,
# plus 2000x1 for the six large-crowd scenarios), both modes, seeds 0/1/2, and
# serial and --workers. 6000 ticks, 100% moving (the screen's settings; the T7
# rapidreplacement 2000x1 s0 baseline hashes were taken this way).
SCENARIOS = ("open", "doors", "bridges", "maze", "opposingcolumns", "sharedgoal",
             "mixedfootprints", "exploration", "dynamicobstacle", "rapidreplacement",
             "unreachable", "recovery", "recovery-passive",
             "jagged", "trapped", "crowdtrap", "singleunit", "groupdetour")
BIG_SCENARIOS = ("doors", "maze", "opposingcolumns", "sharedgoal", "rapidreplacement",
                 "dynamicobstacle")
# singleunit's spawn tiling aborts above ~1000 units (T8 housekeeping item), so
# its "500x4" population is 250x4 until that fix lands.
POPULATIONS = {"singleunit": ((200, 1), (250, 4))}


def matrix_full():
    rows = []
    for scenario in SCENARIOS:
        pops = list(POPULATIONS.get(scenario, ((200, 1), (500, 4))))
        if scenario in BIG_SCENARIOS:
            pops.append((2000, 1))
        for units, players in pops:
            for mode in MODES:
                for seed in SEEDS:
                    for execution in EXECUTIONS:
                        rows.append((scenario, units, players, seed, mode, execution))
    return rows


# Quick matrix (12 rows, about a minute per build on 4 cores): the two T7
# baseline rows, then one row each for the scenario families Stage A touches
# (shared goals, doors/bridges, opposing traffic, obstacles, detours), mixing
# seeds, populations and serial/--workers, plus two Retail controls.
MATRIX_QUICK = (
    ("rapidreplacement", 2000, 1, 0, "legion", "serial"),
    ("rapidreplacement", 2000, 1, 0, "retail", "serial"),
    ("open", 200, 1, 0, "legion", "workers"),
    ("doors", 500, 4, 1, "legion", "serial"),
    ("bridges", 200, 1, 2, "legion", "serial"),
    ("sharedgoal", 500, 4, 0, "legion", "workers"),
    ("opposingcolumns", 200, 1, 1, "legion", "serial"),
    ("dynamicobstacle", 200, 1, 0, "legion", "serial"),
    ("groupdetour", 200, 1, 0, "legion", "serial"),
    ("jagged", 200, 1, 1, "legion", "workers"),
    ("crowdtrap", 200, 1, 2, "legion", "serial"),
    ("maze", 200, 1, 2, "retail", "workers"),
)

TOV1_EMPTY = hashlib.sha256(b"TOV1" + struct.pack("<II", 0, 0)).hexdigest().encode()
TOV2_EMPTY = hashlib.sha256(b"TOV2" + struct.pack("<II", 0, 0)).hexdigest().encode()


def patch_replay(args):
    data = bytearray(Path(args.src).read_bytes())
    if data[:4] != b"TAKR":
        raise SystemExit(f"{args.src}: not a .takrep")
    old = struct.unpack_from("<I", data, 8)[0]
    struct.pack_into("<I", data, 8, args.protocol)
    swapped = 0
    if args.protocol >= 239:
        at = data.find(TOV1_EMPTY, 0, 4096)
        if at >= 0:
            data[at:at + len(TOV2_EMPTY)] = TOV2_EMPTY
            swapped = 1
    Path(args.dst).write_bytes(bytes(data))
    print(f"{Path(args.src).name}: protocol {old} -> {args.protocol}, tov1->tov2 {swapped}")


# ---------------------------------------------------------------- crowdbench

_live = {}
_live_lock = threading.Lock()
_stopping = threading.Event()


def _stop(signum, _frame):
    _stopping.set()
    with _live_lock:
        for proc in list(_live.values()):
            try:
                proc.kill()   # only the PIDs this runner started
            except OSError:
                pass
    raise SystemExit(128 + signum)


def row_name(row):
    scenario, units, players, seed, mode, execution = row
    return f"{scenario} {units}x{players} s{seed} {mode} {execution}"


def run_crowdbench_job(role, binary, row, outdir, cores, env):
    scenario, units, players, seed, mode, execution = row
    cmd = ["taskset", "-c", cores, binary, "--scenario", scenario, "--units", str(units),
           "--players", str(players), "--moving-percent", str(MOVING), "--mode", mode,
           "--ticks", str(TICKS), "--seed", str(seed)]
    if execution == "workers":
        cmd.append("--workers")
    stem = row_name(row).replace(" ", "_")
    log = outdir / f"{role}-{stem}.log"
    record = {"role": role, "row": row_name(row), "command": cmd}
    if _stopping.is_set():
        record["error"] = "stopped"
        return record
    start = time.time()
    with open(log, "w") as out:
        proc = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env=env)
        with _live_lock:
            _live[proc.pid] = proc
        try:
            rc = proc.wait(timeout=3600)
        except subprocess.TimeoutExpired:
            proc.kill()
            rc = proc.wait()
            record["error"] = "timeout"
        finally:
            with _live_lock:
                _live.pop(proc.pid, None)
    record["returncode"] = rc
    record["seconds"] = round(time.time() - start, 2)
    try:
        line = next(l for l in reversed(log.read_text().splitlines()) if l.startswith("{"))
        data = json.loads(line)
        record["hash"] = data.get("hash")
        for key in ("arrived_settled", "crossed_middle", "stalled_unit_ticks", "alive"):
            record[key] = data.get(key)
        if rc and "error" not in record:
            record["error"] = f"exit {rc}"
    except (StopIteration, ValueError) as error:
        record.setdefault("error", f"exit {rc}, no JSON ({error})")
    return record


def crowdbench(args):
    signal.signal(signal.SIGTERM, _stop)
    signal.signal(signal.SIGINT, _stop)
    rows = list(MATRIX_QUICK) if args.quick else matrix_full()
    outdir = Path(args.out) / "crowdbench"
    outdir.mkdir(parents=True, exist_ok=True)
    jobs = []
    for row in rows:
        for role, binary in (("base", args.base), ("cand", args.cand)):
            env = dict(os.environ)
            env.pop("TAK_LEGION_VERIFY", None)
            if role == "cand" and args.verify:
                env["TAK_LEGION_VERIFY"] = "1"
            jobs.append((role, binary, row, env))
    # Longest first so the pool drains evenly.
    jobs.sort(key=lambda j: -(j[2][1] * j[2][2]))
    width = max(1, cpu_count(args.cores))
    results = {role: [] for role in ROLES}
    started = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=width) as pool:
        futures = [pool.submit(run_crowdbench_job, role, binary, row, outdir, args.cores, env)
                   for role, binary, row, env in jobs]
        done = 0
        for future in concurrent.futures.as_completed(futures):
            record = future.result()
            results[record["role"]].append(record)
            done += 1
            if done % 50 == 0 or done == len(futures):
                print(f"crowdbench {done}/{len(futures)} ({time.time() - started:.0f} s)", flush=True)
    for role in ROLES:
        with open(outdir / f"{role}.jsonl", "w") as handle:
            for record in sorted(results[role], key=lambda r: r["row"]):
                handle.write(json.dumps(record) + "\n")


def cpu_count(spec):
    total = 0
    for part in spec.split(","):
        if "-" in part:
            a, b = part.split("-")
            total += int(b) - int(a) + 1
        elif part:
            total += 1
    return total


# --------------------------------------------------------------------- report

def short(value):
    return "-" if value is None else str(value)


def digest(items):
    return hashlib.sha256("\n".join(items).encode()).hexdigest()[:16]


class Report:
    def __init__(self, out):
        self.out = Path(out)
        self.rows = []

    def add(self, section, check, base, cand, status, note=""):
        self.rows.append({"section": section, "check": check, "base": short(base),
                          "cand": short(cand), "status": status, "note": note})

    def compare(self, section, check, base, cand, note=""):
        if base is None or cand is None:
            self.add(section, check, base, cand, "FAIL", note or "missing result")
        else:
            self.add(section, check, base, cand, "SAME" if base == cand else "DIFF", note)


def read_meta(out):
    meta = {}
    path = Path(out) / "meta.txt"
    if path.exists():
        for line in path.read_text().splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                meta[key] = value
    return meta


REPLAY_DONE = re.compile(r"replay done: tick=(\d+) hash=([0-9a-f]+) units=(\d+)")
REPLAY_DIVERGED = re.compile(r"replay: DIVERGED at tick (\d+) -- recorded ([0-9a-f]+), replayed ([0-9a-f]+)")
HASHDETAIL = re.compile(r"^HASHDETAIL t=(\d+) section=(\S+) hash=([0-9a-f]+)")


def parse_replay(path):
    """Final hash plus the per-checkpoint section hashes.

    A debug takclient with TAK_HASHDETAIL prints every section prefix of
    World::stateHash() each time it is computed; in replay mode that is at each
    recorded checkpoint (every 300 ticks) and at the end. The section hashes up
    to 'players' cover units, Legion, scripts and grades; the full hash adds the
    features/RNG tail, and the final full hash is the 'replay done' line."""
    if not path.exists():
        return None
    text = path.read_text(errors="replace")
    result = {"final": None, "tick": None, "units": None, "diverged": None, "checkpoints": []}
    match = REPLAY_DONE.search(text)
    if match:
        result.update(tick=int(match[1]), final=match[2], units=int(match[3]))
    match = REPLAY_DIVERGED.search(text)
    if match:
        result["diverged"] = {"tick": int(match[1]), "recorded": match[2], "replayed": match[3]}
    seen = {}
    for line in text.splitlines():
        match = HASHDETAIL.match(line)
        if match:
            seen.setdefault(int(match[1]), {})[match[2]] = match[3]
    result["checkpoints"] = [{"tick": tick, "sections": sections}
                             for tick, sections in sorted(seen.items())]
    return result


def report_replays(report, meta, details):
    names = [n for n in meta.get("replays", "").split(",") if n]
    for name in names:
        parsed = {}
        skipped = None
        for role in ROLES:
            if meta.get(f"{role}_debug") != "1":
                skipped = f"{role} is a release takclient (no TAK_REPLAY_VERIFY)"
            parsed[role] = parse_replay(report.out / role / f"replay-{name}.log")
        if skipped:
            report.add("replay", f"{name} final", None, None, "SKIP", skipped)
            continue
        base, cand = parsed["base"], parsed["cand"]
        details[f"replay {name}"] = parsed
        bf = base and base["final"] and f"{base['final']} t={base['tick']} u={base['units']}"
        cf = cand and cand["final"] and f"{cand['final']} t={cand['tick']} u={cand['units']}"
        note = f"protocol {meta.get('base_protocol')}->{meta.get('cand_protocol')} patch"
        report.compare("replay", f"{name} final", bf, cf, note)
        bc = base["checkpoints"] if base else []
        cc = cand["checkpoints"] if cand else []
        if not bc or not cc:
            report.add("replay", f"{name} checkpoints", len(bc), len(cc), "FAIL",
                       "no HASHDETAIL lines (replay did not run?)")
            continue
        flat = lambda cps: [f"{c['tick']}:{s}:{h}" for c in cps for s, h in c["sections"].items()]
        bd, cd = digest(flat(bc)), digest(flat(cc))
        note = ""
        if bd != cd:
            cmap = {c["tick"]: c["sections"] for c in cc}
            for c in bc:
                other = cmap.get(c["tick"])
                if other != c["sections"]:
                    section = next((s for s, h in c["sections"].items()
                                    if (other or {}).get(s) != h), "?")
                    note = f"first difference at tick {c['tick']} section {section}"
                    break
            else:
                note = "checkpoint ticks differ"
        report.add("replay", f"{name} checkpoints",
                   f"{len(bc)} @ {bd}", f"{len(cc)} @ {cd}",
                   "SAME" if bd == cd else "DIFF", note)
        bdv, cdv = base["diverged"], cand["diverged"]
        bdv = bdv and f"t{bdv['tick']} {bdv['replayed']}"
        cdv = cdv and f"t{cdv['tick']} {cdv['replayed']}"
        report.add("replay", f"{name} vs recording", bdv or "exact", cdv or "exact",
                   "SAME" if bdv == cdv else "DIFF",
                   "first checkpoint off the recording (a replay made on an older sim)"
                   if bdv or cdv else "plays back exactly")


def report_crowdbench(report, details):
    path = {role: report.out / "crowdbench" / f"{role}.jsonl" for role in ROLES}
    if not all(p.exists() for p in path.values()):
        if any(p.exists() for p in path.values()) or (report.out / "crowdbench").exists():
            report.add("crowdbench", "matrix", None, None, "FAIL", "results missing")
        return
    data = {role: {json.loads(l)["row"]: json.loads(l) for l in path[role].read_text().splitlines() if l}
            for role in ROLES}
    details["crowdbench"] = data
    for row in sorted(set(data["base"]) | set(data["cand"])):
        b, c = data["base"].get(row), data["cand"].get(row)
        if not b or not c or b.get("error") or c.get("error") or not b.get("hash") or not c.get("hash"):
            note = "; ".join(f"{r}: {x.get('error')}" for r, x in (("base", b), ("cand", c))
                             if x and x.get("error")) or "missing row"
            report.add("crowdbench", row, b and b.get("hash"), c and c.get("hash"), "FAIL", note)
        else:
            report.compare("crowdbench", row, b["hash"], c["hash"])
    for role in ROLES:
        pairs = {}
        for row, record in data[role].items():
            if record.get("hash"):
                key, execution = row.rsplit(" ", 1)
                pairs.setdefault(key, {})[execution] = record["hash"]
        both = [p for p in pairs.values() if len(p) == 2]
        bad = [k for k, p in pairs.items() if len(p) == 2 and p["serial"] != p["workers"]]
        if both:
            report.add("crowdbench", f"serial == workers ({role})", f"{len(both)} pairs",
                       f"{len(bad)} differ", "SAME" if not bad else "FAIL",
                       ", ".join(bad[:4]))


def parse_junit(path):
    if not path.exists():
        return None
    tests = {}
    for case in ET.parse(path).getroot().iter("testcase"):
        status = case.get("status") or "run"
        if case.find("failure") is not None or case.find("error") is not None:
            status = "fail"
        elif case.find("skipped") is not None and status == "run":
            status = "notrun"
        out = case.findtext("system-out") or ""
        hashes = re.findall(r"\bhash[=: ]+(?:0x)?([0-9a-f]{12,16})\b", out)
        tests[case.get("name")] = {"status": status, "hashes": hashes,
                                   "time": float(case.get("time") or 0)}
    return tests


def report_ctest(report, details):
    junit = {role: parse_junit(report.out / role / "ctest.xml") for role in ROLES}
    if junit["base"] is None and junit["cand"] is None:
        return
    if junit["base"] is None or junit["cand"] is None:
        report.add("ctest", "junit", junit["base"] is not None, junit["cand"] is not None,
                   "FAIL", "ctest produced no junit file")
        return
    details["ctest"] = junit
    for name in sorted(set(junit["base"]) | set(junit["cand"])):
        b, c = junit["base"].get(name), junit["cand"].get(name)
        label = lambda t: None if t is None else (
            t["status"].upper().replace("RUN", "PASS") +
            (f" h:{digest(t['hashes'])}" if t["hashes"] else ""))
        if b is None or c is None:
            report.add("ctest", name, label(b), label(c), "FAIL", "test missing in one build")
            continue
        disabled = {"disabled", "notrun"}
        if b["status"] in disabled and c["status"] in disabled:
            report.add("ctest", name, label(b), label(c), "SKIP", "disabled")
            continue
        if b["status"] != "run" or c["status"] != "run":
            report.add("ctest", name, label(b), label(c), "FAIL", "test failed")
            continue
        status = "SAME" if b["hashes"] == c["hashes"] else "DIFF"
        note = "printed hashes differ" if status == "DIFF" else ""
        report.add("ctest", name, label(b), label(c), status, note)


def report_golden(report, details):
    for mode in MODES:
        lines = {}
        for role in ROLES:
            path = report.out / role / f"golden-{mode}.txt"
            lines[role] = ([l for l in path.read_text().splitlines() if l.startswith("mode=")]
                           if path.exists() else None)
        if lines["base"] is None and lines["cand"] is None:
            continue
        details[f"golden {mode}"] = lines
        show = lambda ls: None if not ls else f"{len(ls)} @ {digest(ls)} last {ls[-1].split('hash=')[-1]}"
        note = ""
        if lines["base"] and lines["cand"] and lines["base"] != lines["cand"]:
            first = next((b for b, c in zip(lines["base"], lines["cand"]) if b != c), None)
            note = f"first difference: {first}"
        if not lines["base"] or not lines["cand"] or len(lines["base"]) != 24:
            report.add("golden", f"navigation {mode} (serial)", show(lines["base"]),
                       show(lines["cand"]), "FAIL", note or "expected 24 checkpoints")
        else:
            report.compare("golden", f"navigation {mode} (serial)", show(lines["base"]),
                           show(lines["cand"]), note)


MPAI_DONE = re.compile(r"mp-headless done: tick=(\d+) hash=([0-9a-f]+) units=(\d+) err=(\S+)")


def report_mpai(report, meta, details):
    runs = int(meta.get("mpai_runs", "0") or 0)
    if not runs:
        return
    for mode in ("legion", "retail"):
        values = {}
        skip = None
        for role in ROLES:
            if meta.get(f"{role}_debug") != "1":
                skip = f"{role} is a release takclient (no --mpai)"
            values[role] = []
            for run in range(1, runs + 1):
                path = report.out / role / f"mpai-{mode}-{run}.log"
                match = MPAI_DONE.search(path.read_text(errors="replace")) if path.exists() else None
                values[role].append(match and {"tick": int(match[1]), "hash": match[2],
                                               "units": int(match[3]), "err": match[4]})
        check = f"--mpai {mode} Inner Circle {meta.get('mpai_time')} s x{runs}"
        if skip:
            report.add("mpai", check, None, None, "SKIP", skip)
            continue
        details[f"mpai {mode}"] = values
        show = lambda vs: "/".join("none" if not v else f"{v['hash']}@{v['tick']}" for v in vs)
        problems = []
        for role in ROLES:
            vs = values[role]
            if not all(vs):
                problems.append(f"{role}: a run printed no result")
            elif any(v["err"] != "none" for v in vs):
                problems.append(f"{role}: err={','.join(v['err'] for v in vs)}")
            elif len({v["hash"] for v in vs}) != 1:
                problems.append(f"{role}: not reproducible run-to-run")
        if problems:
            report.add("mpai", check, show(values["base"]), show(values["cand"]), "FAIL",
                       "; ".join(problems))
        else:
            report.compare("mpai", check, show(values["base"]), show(values["cand"]),
                           "reproducible run-to-run in both builds")


DET_OK = re.compile(r"DETERMINISM OK -- every build agrees on golden (\S+)")


def report_determinism(report, details):
    values = {}
    for role in ROLES:
        path = report.out / role / "determinism.log"
        if not path.exists():
            values[role] = None
            continue
        text = path.read_text(errors="replace")
        match = DET_OK.search(text)
        values[role] = match[1] if match else "FAILED"
    if values["base"] is None and values["cand"] is None:
        return
    details["determinism"] = values
    if "FAILED" in values.values() or None in values.values():
        report.add("determinism", "check-determinism.sh golden", values["base"], values["cand"],
                   "FAIL", "check-determinism.sh did not pass")
    else:
        report.compare("determinism", "check-determinism.sh golden", values["base"], values["cand"])


def render(rows, collapse):
    widths = [max(len(r[k]) for r in rows + [{"section": "section", "check": "check", "base": "base",
                                              "cand": "cand", "status": "status", "note": ""}])
              for k in ("section", "check", "base", "cand", "status")]
    widths = [min(w, 48) for w in widths]
    def line(r):
        cells = [r[k][:48].ljust(w) for k, w in zip(("section", "check", "base", "cand", "status"), widths)]
        return ("| " + " | ".join(cells) + " | " + r.get("note", "")).rstrip()
    out = [line({"section": "section", "check": "check", "base": "base", "cand": "cand",
                 "status": "status", "note": "note"})]
    hidden = 0
    for r in rows:
        if collapse and r["section"] == "crowdbench" and r["status"] == "SAME" \
                and not r["check"].startswith("serial =="):
            hidden += 1
            continue
        out.append(line(r))
    if hidden:
        out.append(line({"section": "crowdbench", "check": f"{hidden} matrix rows",
                         "base": "(see identity.txt)", "cand": "", "status": "SAME",
                         "note": "every hidden row SAME"}))
    return "\n".join(out)


def report(args):
    out = Path(args.out)
    meta = read_meta(out)
    rep = Report(out)
    details = {}
    report_replays(rep, meta, details)
    report_crowdbench(rep, details)
    report_golden(rep, details)
    report_ctest(rep, details)
    report_mpai(rep, meta, details)
    report_determinism(rep, details)
    counts = {}
    for row in rep.rows:
        counts[row["status"]] = counts.get(row["status"], 0) + 1
    ok = bool(rep.rows) and not counts.get("DIFF") and not counts.get("FAIL")
    timings = {}
    tpath = out / "timings.txt"
    if tpath.exists():
        for line in tpath.read_text().splitlines():
            if line.strip():
                name, seconds = line.split()
                timings[name] = int(seconds)
    summary = (f"legion_identity: {'PASS' if ok else 'FAIL'} -- " +
               ", ".join(f"{counts[k]} {k}" for k in ("SAME", "DIFF", "FAIL", "SKIP") if k in counts) +
               (f"; wall {timings.get('total')} s" if "total" in timings else ""))
    full = render(rep.rows, collapse=False)
    (out / "identity.txt").write_text(
        f"base {meta.get('base')} ({meta.get('base_type')}, {meta.get('base_rev')})\n"
        f"cand {meta.get('cand')} ({meta.get('cand_type')}, {meta.get('cand_rev')})\n\n"
        + full + "\n\n" + summary + "\n")
    (out / "identity.json").write_text(json.dumps(
        {"schema": 1, "ok": ok, "counts": counts, "meta": meta, "timings": timings,
         "rows": rep.rows, "details": details}, indent=1) + "\n")
    print(f"base {meta.get('base')} ({meta.get('base_type')}, {meta.get('base_rev')})")
    print(f"cand {meta.get('cand')} ({meta.get('cand_type')}, {meta.get('cand_rev')})")
    print(render(rep.rows, collapse=True))
    print(summary)
    print(f"full table: {out / 'identity.txt'}  json: {out / 'identity.json'}")
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("patch-replay")
    p.add_argument("src")
    p.add_argument("dst")
    p.add_argument("protocol", type=int)
    p = sub.add_parser("crowdbench")
    p.add_argument("--out", required=True)
    p.add_argument("--base", required=True)
    p.add_argument("--cand", required=True)
    p.add_argument("--cores", required=True)
    p.add_argument("--quick", action="store_true")
    p.add_argument("--verify", action="store_true")
    p = sub.add_parser("report")
    p.add_argument("--out", required=True)
    args = parser.parse_args()
    if args.command == "patch-replay":
        patch_replay(args)
    elif args.command == "crowdbench":
        crowdbench(args)
    else:
        sys.exit(report(args))


if __name__ == "__main__":
    main()
