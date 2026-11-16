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

The same columns sent with ONE right-click to one point (overtake-click-*, as a player sends a mixed selection), and
a mixed block of 16 slow bodies in front of 16 fast ones (overtake-click-blob). Passing within one click (clickPass,
overtakeSlot) before this, parent 2103fb83, where a body never passed one of its own click:
  overtake-click-field (attack-move), offsets 0, +-1..3: fast mean 1,186..1,712, 1,223 fast stops; now 914..958, 18.
    Slow mean 2,361..2,463 and last 2,638..2,776; now 2,376..2,448 and at most 2,778. Checked: the fast mean at most
    1,050 at every offset, at most 60 fast stops in all, the slow mean at most 2,500 and their last at most 2,800.
  overtake-click-road (move), offsets 0, +-1..3: fast mean 1,258..1,743, 1,407 stops; now 954..998, 60. Slow mean
    2,299..2,416, last at most 2,573; now 2,258..2,421 and 2,619. Checked: fast mean at most 1,050, at most 150 fast
    stops, slow mean at most 2,450 and last at most 2,650.
  overtake-click-corridor (no room), offsets 0, +-1, 2: fast mean 2,170..2,205, slow last 2,457..2,732; now
    2,082..2,141 and 2,457..2,668. Checked: fast mean at most 2,250, slow last at most 2,780.
  overtake-click-blob (a block of 32, a crowd: no pass), offsets 0, +-1..3: fast done 1,975..2,091, slow done
    2,127..2,754, unchanged. Checked: fast done at most 2,150, slow done at most 2,800.
  overtake-click-choke (a block of 40 through a gap 12 cells wide), offsets 0, +-1, 6: commit dbe3149a passed within
    the click past the gap, 30-40 cells short of the point, at offsets 0, -1 and 6, and a slow body paid each time
    (offset 0: one not done, against the last at 2,895 without passes). Checked: no pass within the click there
    (clickPass keeps out of kOvertakeHome cells round the click's area), and at most one slow body not done.
One slow body of a one-click column settles just outside its disc at one offset of the road and of the corridor, on
the parent as now; a fast body never does.

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
        r = {"offset": off, "overtakes": per("work.overtakes.total")[i],
             "click_overtakes": per("work.click_overtakes.total")[i]}
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
    bad += clicks(exe, scenarios)
    for b in bad:
        print("FAIL:", b)
    return 1 if bad else 0


def clicks(exe, scenarios):
    """The one-click fixtures; returns the failures (a tooling failure is one too)."""
    bad = []
    runs = {}
    for name, offsets in (("overtake-click-field", "0,1,-1,2,-2,3,-3"), ("overtake-click-road", "0,1,-1,2,-2,3,-3"),
                          ("overtake-click-corridor", "0,1,-1,2"), ("overtake-click-blob", "0,1,-1,2,-2,3,-3"),
                          ("overtake-click-choke", "0,1,-1,6")):
        rows = run(exe, scenarios, name, offsets)
        if rows is None:
            return [f"{name}: legion_scenario failed"]
        runs[name] = rows
        late = 0
        for r in rows:
            sdone = [d for d in r["Sdone"] if d >= 0]
            r["Smean"] = statistics.mean(sdone) if sdone else 1 << 30
            r["Slast"] = max(sdone) if sdone else 1 << 30
            late += len(r["Sdone"]) - len(sdone)
            fmean = statistics.mean(r["Fdone"]) if min(r["Fdone"]) >= 0 else -1
            print(f"{name} offset {r['offset']:+d}: fast done {r['Fdone']} mean {fmean:.0f} stops {r['Fstops']}; "
                  f"slow mean {r['Smean']:.0f} last {r['Slast']} not done {len(r['Sdone']) - len(sdone)}; "
                  f"overtakes {r['overtakes']}")
            if min(r["Fdone"]) < 0:
                bad.append(f"{name} offset {r['offset']}: a fast body never arrived")
        if late > 1:
            bad.append(f"{name}: {late} slow bodies not done (at most 1)")
    if bad:
        return bad
    limits = {"overtake-click-field": (1050, 60, 2500, 2800), "overtake-click-road": (1050, 150, 2450, 2650)}
    for name, (fmax, smax, slowmean, slowlast) in limits.items():
        rows = runs[name]
        for r in rows:
            if statistics.mean(r["Fdone"]) > fmax:
                bad.append(f"{name} offset {r['offset']}: fast mean {statistics.mean(r['Fdone']):.0f} "
                           f"(at most {fmax:,})")
            if r["Smean"] > slowmean:
                bad.append(f"{name} offset {r['offset']}: slow mean {r['Smean']:.0f} (at most {slowmean:,})")
            if r["Slast"] > slowlast:
                bad.append(f"{name} offset {r['offset']}: last slow body {r['Slast']} (at most {slowlast:,})")
        if sum(r["Fstops"] for r in rows) > smax:
            bad.append(f"{name}: {sum(r['Fstops'] for r in rows)} fast stops (at most {smax})")
    for r in runs["overtake-click-corridor"]:
        if statistics.mean(r["Fdone"]) > 2250:
            bad.append(f"overtake-click-corridor offset {r['offset']}: fast mean "
                       f"{statistics.mean(r['Fdone']):.0f} (at most 2,250)")
        if r["Slast"] > 2780:
            bad.append(f"overtake-click-corridor offset {r['offset']}: last slow body {r['Slast']} (at most 2,780)")
    for r in runs["overtake-click-blob"]:
        if max(r["Fdone"]) > 2150:
            bad.append(f"overtake-click-blob offset {r['offset']}: fast done {max(r['Fdone'])} (at most 2,150)")
        if r["Slast"] > 2800:
            bad.append(f"overtake-click-blob offset {r['offset']}: slow done {r['Slast']} (at most 2,800)")
    for r in runs["overtake-click-choke"]:
        if r["click_overtakes"]:
            bad.append(f"overtake-click-choke offset {r['offset']}: {r['click_overtakes']} passes within the click "
                       "near its point (none)")
    return bad


if __name__ == "__main__":
    sys.exit(main())
