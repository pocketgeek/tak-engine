#!/usr/bin/env python3
"""Run serialized crowdbench comparisons; keep the host otherwise idle for timings.

One units value is PER PLAYER. Population entries are players:moving-percent.
Each invocation records the immutable binary hash, exact commands, raw JSON,
errors, and run times. Repeated rounds reverse order to expose order effects.
This runner never builds, modifies the engine, or starts a public server.
"""
import argparse
import csv
import hashlib
import itertools
import json
from pathlib import Path
import platform
import subprocess
import time

SCENARIOS = (
    "open", "doors", "bridges", "maze", "opposingcolumns", "sharedgoal",
    "mixedfootprints", "exploration", "dynamicobstacle", "rapidreplacement",
    "unreachable", "recovery", "recovery-passive",
)


def sha256(path):
    with open(path, "rb") as handle:
        return hashlib.file_digest(handle, "sha256").hexdigest()


def compare_traces(left, right):
    """Compare all ticks and controller transitions, not just the final hash."""
    for suffix in ("", ".units.csv"):
        a, b = Path(str(left) + suffix), Path(str(right) + suffix)
        with a.open(newline="") as first, b.open(newline="") as second:
            for line, (x, y) in enumerate(itertools.zip_longest(csv.reader(first), csv.reader(second)), 1):
                if x != y:
                    raise SystemExit(f"trace mismatch {suffix or 'ticks'} line {line}: {x!r} != {y!r}")
        print(f"identical: {a} / {b}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--reference-binary", type=Path,
                        help="interleave frozen reference cases with the candidate for matched timings")
    parser.add_argument("--reference-modes", nargs="+", default=["retail"],
                        choices=["retail", "flowfield"])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--label", default="unlabelled")
    parser.add_argument("--modes", nargs="+", default=["retail", "retail-plus", "flowfield"],
                        choices=["retail", "retail-plus", "flowfield"])
    parser.add_argument("--units", nargs="+", type=int, default=[200, 500, 1000, 2000])
    parser.add_argument("--populations", nargs="+", default=["1:100", "4:25", "8:10", "4:100"],
                        help="players:moving-percent pairs, not total units")
    parser.add_argument("--scenarios", nargs="+", choices=SCENARIOS, default=["open"])
    parser.add_argument("--ticks", type=int, default=1200)
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--workers", action="store_true")
    parser.add_argument("--trace", action="store_true", help="every-tick traces; larger files and observer overhead")
    parser.add_argument("--allocations", action="store_true", help="separate diagnostic run; changes timing overhead")
    parser.add_argument("--profile", action="store_true", help="opt-in new-build search clock counters")
    parser.add_argument("--compare-traces", nargs=2, type=Path)
    args = parser.parse_args()
    if args.compare_traces:
        compare_traces(*args.compare_traces)
        return
    if args.binary is None or args.output is None:
        parser.error("--binary and --output are required")
    binary = args.binary.resolve(strict=True)
    reference = args.reference_binary.resolve(strict=True) if args.reference_binary else None
    if args.rounds < 1 or args.ticks < 1:
        parser.error("rounds and ticks must be positive")
    populations = []
    for value in args.populations:
        players, moving = map(int, value.split(":"))
        if not 1 <= players <= 8 or not 0 <= moving <= 100:
            parser.error("population needs players 1..8 and moving-percent 0..100")
        populations.append((players, moving))
    args.output.mkdir(parents=True, exist_ok=True)
    manifest_path = args.output / "manifest.json"
    if manifest_path.exists():
        parser.error("output already contains a manifest; choose a new directory")
    manifest = {
        "label": args.label, "binary": str(binary), "binary_sha256": sha256(binary),
        "host": platform.platform(), "machine": platform.machine(), "started_unix": time.time(),
        "arguments": {key: str(value) if isinstance(value, Path) else value
                      for key, value in vars(args).items()},
        "measurement": "World.tick only; serial subprocesses; host quietness must be coordinated externally",
        "arrival": "live, empty orders, zero speed, legal full footprint, authored goal area, 30 unchanged ticks",
        "runs": [],
    }
    if reference:
        manifest["reference_binary"] = str(reference)
        manifest["reference_binary_sha256"] = sha256(reference)
    variants = ([("reference", reference, mode) for mode in args.reference_modes] if reference else [])
    variants += [("candidate", binary, mode) for mode in args.modes]
    cases = list(itertools.product(args.scenarios, args.units, populations, variants))
    errors = 0
    with (args.output / "results.jsonl").open("w") as results:
        for repeat in range(args.rounds):
            ordered = cases if repeat % 2 == 0 else list(reversed(cases))
            for scenario, units, (players, moving), (role, executable, mode) in ordered:
                role_prefix = "reference-" if role == "reference" else ""
                name = f"r{repeat}-{scenario}-{units}x{players}-m{moving}-{role_prefix}{mode}"
                command = [str(executable), "--scenario", scenario, "--units", str(units),
                           "--players", str(players), "--moving-percent", str(moving),
                           "--mode", mode, "--ticks", str(args.ticks)]
                for flag in ("workers", "allocations", "profile"):
                    if getattr(args, flag):
                        command.append("--" + flag)
                if args.trace:
                    command += ["--trace", str((args.output / f"{name}.trace.csv").resolve())]
                print(f"start {name}", flush=True)
                record = {"name": name, "round": repeat, "build_role": role,
                          "command": command, "start_unix": time.time()}
                try:
                    with (args.output / f"{name}.stdout").open("w") as stdout, \
                            (args.output / f"{name}.stderr").open("w") as stderr:
                        result = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=args.timeout)
                    record["returncode"] = result.returncode
                    if result.returncode:
                        raise RuntimeError(f"exit {result.returncode}")
                    lines = (args.output / f"{name}.stdout").read_text().splitlines()
                    data = json.loads(next(line for line in reversed(lines) if line.startswith("{")))
                    record["metrics"] = data
                    results.write(json.dumps({"name": name, "round": repeat,
                                              "build_role": role, **data}) + "\n")
                    results.flush()
                    print(f"done {name}: {data['arrived_settled']}/{data['moving_units']} settled, "
                          f"mean {data['tick_ms_mean']:.3f} ms", flush=True)
                except (subprocess.TimeoutExpired, RuntimeError, ValueError, StopIteration) as error:
                    record["error"] = str(error)
                    errors += 1
                    print(f"FAILED {name}: {error}", flush=True)
                record["end_unix"] = time.time()
                manifest["runs"].append(record)
                manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    manifest["finished_unix"] = time.time()
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    if errors:
        raise SystemExit(f"{errors} cases failed; see manifest and stderr files")


if __name__ == "__main__":
    main()
