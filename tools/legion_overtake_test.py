#!/usr/bin/env python3
"""legion_overtake_test -- overtaking a slower ally (2026-11-15).

    legion_overtake_test.py LEGION_SCENARIO SCENARIO_DIR

A single-file column of 12 bodies walks north, slow 2x2 bodies (speed 1.15, an Aramon Archer's) and fast 3x3 ones
(2.9, an Aramon Knight's) mixed, each on an order of its own, as the computer player sends its army. Before, a fast
body behind a slow one held, walked a cell when the slow one had walked one and held again, all the way: flow-around
takes one cell, and a 3x3 body behind a 2x2 one needs two or three cells sideways before its lane is clear. It now
steps aside and passes where there is room (overtakeRoute in src/sim/legion.cpp). All asset-free, all offsets
spawn inside the walls.

  overtake-field (open ground, attack-move orders), offsets 0, +-1..3. Parent 870296cf: fast bodies' last arrival
    1,143..1,687 (over 1,200 at 4 of 7 offsets), 70 stops in all; now 1,128..1,148, none. Checked: the last fast
    body done by tick 1,200 at every offset, at most 10 fast stops in all, at least one overtake.
  overtake-road (a road 8 cells wide, move orders), offsets 0, +-1..3. Parent: fast bodies' mean arrival median
    1,674 (1,533..2,182), 1,558 stops; now median 1,064 (992..1,575), 173 stops. Checked: that median at most 1,300,
    at most 400 fast stops in all.
  overtake-corridor (4 cells wide: no room to pass, attack-move orders), offsets 0, +-1, 2. Parent: fast mean
    2,178..2,230, slow bodies' last 2,709..2,761; now 2,117..2,171 and 2,682..2,734. Checked: no worse -- the fast
    mean at most 2,250 and the slow bodies' last at most 2,780 at every offset.
Every body arrives in all three, and the slow bodies' last arrival is no later than tick 2,800 on field and road
(parent 2,640..2,784): nobody pays for a pass.

Exit 0 pass, 1 fail, 2 tooling.
"""
import json
import os
import statistics
import subprocess
import sys


def run(exe, scenarios, name, offsets):
    scn = os.path.join(scenarios, name + ".scn")
    p = subprocess.run([exe, scn, "--mode", "legion", "--offsets", offsets, "--serial", "--json"],
                       capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stdout[-2000:], p.stderr[-4000:])
        return None
    rows = [json.loads(ln) for ln in p.stdout.splitlines() if ln.startswith("{")]
    if not rows:
        return None
    keys, varying, offs = rows[0]["keys"], rows[0].get("varying", {}), rows[0]["offsets"]
    groups = sorted({k.split(".")[1] for k in keys if k.startswith("g.") and k.endswith(".n")})

    def per(key):
        return varying.get(key, [keys.get(key, 0)] * len(offs))   # no overtakes key before the rule

    out = []
    for i, off in enumerate(offs):
        r = {"offset": off, "overtakes": per("work.overtakes.total")[i]}
        for kind in "FS":
            mine = [g for g in groups if g.startswith(kind)]
            done = [per(f"g.{g}.done")[i] for g in mine]
            r[kind + "done"] = done
            r[kind + "stops"] = sum(per(f"g.{g}.stops")[i] for g in mine)
        out.append(r)
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    exe, scenarios = sys.argv[1], sys.argv[2]
    bad = []
    runs = {}
    for name, offsets in (("overtake-field", "0,1,-1,2,-2,3,-3"), ("overtake-road", "0,1,-1,2,-2,3,-3"),
                          ("overtake-corridor", "0,1,-1,2")):
        rows = run(exe, scenarios, name, offsets)
        if rows is None:
            return 2
        runs[name] = rows
        for r in rows:
            fmean = statistics.mean(r["Fdone"]) if min(r["Fdone"]) >= 0 else -1
            print(f"{name} offset {r['offset']:+d}: fast done {r['Fdone']} mean {fmean:.0f} stops {r['Fstops']}; "
                  f"slow last {max(r['Sdone'])} stops {r['Sstops']}; overtakes {r['overtakes']}")
            if min(r["Fdone"] + r["Sdone"]) < 0:
                bad.append(f"{name} offset {r['offset']}: a body never arrived")
    if bad:
        for b in bad:
            print("FAIL:", b)
        return 1
    field, road, corridor = runs["overtake-field"], runs["overtake-road"], runs["overtake-corridor"]
    for r in field:
        if max(r["Fdone"]) > 1200:
            bad.append(f"overtake-field offset {r['offset']}: last fast body done at {max(r['Fdone'])} (at most 1,200)")
    if sum(r["Fstops"] for r in field) > 10:
        bad.append(f"overtake-field: {sum(r['Fstops'] for r in field)} fast stops (at most 10)")
    if sum(r["overtakes"] for r in field) < 1:
        bad.append("overtake-field: no overtake")
    median = statistics.median(statistics.mean(r["Fdone"]) for r in road)
    if median > 1300:
        bad.append(f"overtake-road: median fast mean arrival {median:.0f} (at most 1,300)")
    if sum(r["Fstops"] for r in road) > 400:
        bad.append(f"overtake-road: {sum(r['Fstops'] for r in road)} fast stops (at most 400)")
    for name, rows in (("overtake-field", field), ("overtake-road", road)):
        for r in rows:
            if max(r["Sdone"]) > 2800:
                bad.append(f"{name} offset {r['offset']}: last slow body done at {max(r['Sdone'])} (at most 2,800)")
    for r in corridor:
        if statistics.mean(r["Fdone"]) > 2250:
            bad.append(f"overtake-corridor offset {r['offset']}: fast mean {statistics.mean(r['Fdone']):.0f} (at most 2,250)")
        if max(r["Sdone"]) > 2780:
            bad.append(f"overtake-corridor offset {r['offset']}: last slow body {max(r['Sdone'])} (at most 2,780)")
    for b in bad:
        print("FAIL:", b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
