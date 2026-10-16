#!/usr/bin/env python3
"""legion_sched_load_test -- the field scheduler under continuous load (Legion 10,000 stage 4).

    legion_sched_load_test.py LEGION_SCENARIO SCN [--budget-div N]

Runs SCN (tools/scenarios/scale/sched-load.scn: 48 squads re-clicked to far tiles every 160 ticks and a rock
walking across the map, so field requests arrive faster than the quota serves them for the whole run) in Legion,
serial and workers, and checks SERVICE, not throughput:

  * serial == workers (the runner fails on any state or key mismatch);
  * the field quota really was saturated (the longest run of fully spent ticks), so the bounds below are
    measured under load;
  * every service class's longest gap between two slices of work for a job that asked for work
    (scale.sched.gap_max.*) within 3 x kAgeStep (age promotion to the top class) + one round-robin rotation of
    the largest possible top class (1,024 fields of kSlice each over the quota): 539 ticks;
  * every class's longest start-request wait (scale.sched.age_max.*) within the promotion time, the
    reservation age and one field tenure (the room a reserved request waits for frees within it): 900 ticks;
  * no body waits longer than 60 ticks for a usable field (scale.wait.max, the frozen scale target);
  * movement goes on: most bodies arrive somewhere over the run (scale.moving.peak, scale.arrive.n > 0).

With --budget-div N the registration, plane-build and awareness budgets are divided (over-budget paths); the
service bounds still hold. Exit 0 pass, 1 fail, 2 tooling.
"""
import json
import subprocess
import sys

K_AGE_STEP, K_RESERVE, K_TENURE = 150, 150, 300
K_SLICE, K_QUOTA, K_FIELDS = 32768, 384000, 1024
GAP_BOUND = 3 * K_AGE_STEP + (K_FIELDS * K_SLICE + K_QUOTA - 1) // K_QUOTA + 1
AGE_BOUND = 3 * K_AGE_STEP + K_RESERVE + K_TENURE
CLASSES = ("waiting", "first_route", "aware_repair", "stale_refresh")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    exe, scn = sys.argv[1], sys.argv[2]
    extra = sys.argv[3:]
    cmd = [exe, scn, "--mode", "legion", "--offsets", "0", "--both-exec", "--json", "--scale"] + extra
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stderr[-4000:])
        print("legion_sched_load_test: runner exit", p.returncode)
        return 1
    try:
        rec = json.loads(p.stdout.strip().splitlines()[-1])
    except Exception as e:
        print("legion_sched_load_test: no JSON line:", e)
        return 2
    k = rec["keys"]
    fails = []

    def need(ok, what):
        print("  %-78s %s" % (what, "ok" if ok else "FAIL"))
        if not ok:
            fails.append(what)

    need(rec.get("serial_eq_workers") is True, "serial == workers")
    peg = k.get("gauge.quota_peg_run_max", 0)
    need(peg >= 100, "the field quota was saturated (longest fully spent run %s ticks >= 100)" % peg)
    for c in CLASSES:
        g = k.get("scale.sched.gap_max." + c, 0)
        need(g <= GAP_BOUND, "longest service gap, %s: %s <= %s" % (c, g, GAP_BOUND))
    for c in CLASSES:
        a = k.get("scale.sched.age_max." + c, 0)
        need(a <= AGE_BOUND, "longest start-request wait, %s: %s <= %s" % (c, a, AGE_BOUND))
    w = k.get("scale.wait.max", 0)
    need(w <= 60, "longest wait for a usable field: %s <= 60 ticks" % w)
    need(k.get("scale.arrive.n", 0) > 0 and k.get("scale.moving.peak", 0) > 0, "bodies move and arrive")
    print("  (promotions %s, fit skips %s, reservations %s, registrations deferred %s)" % (
        k.get("scale.sched.promotions"), k.get("scale.sched.fit_skips"), k.get("scale.sched.reservations"),
        k.get("scale.sched.reg_deferred")))
    if fails:
        print("legion_sched_load_test: %d FAILED" % len(fails))
        return 1
    print("legion_sched_load_test: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
