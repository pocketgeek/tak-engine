#!/usr/bin/env python3
"""legion_baseline_reasons -- the ctest of the Legion baseline gate.

  legion_baseline_reasons_test.py [BASELINE.json]

1. Fails (exit 1) when the committed baseline (default
   tools/scenarios/baseline.json) has an entry, an exceptions entry or an
   accepted_regressions entry without a reason, an exception without a cluster
   id, or an accepted regression without a limit (tools/legion_check.py
   validate_baseline).
2. Runs the gate's unit tests on synthetic JSON (tools/legion_check.py): an eq
   violation fails, a one-sided loss beyond the band fails, an improvement
   prints RATCHET and passes, a missing reason fails this very test, a
   floor-passing key that starts failing the floor fails inside its band, a
   declared tolerance licenses its loss only for its step and key, and so on.
"""
import contextlib
import copy
import io
import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import legion_check as lc  # noqa: E402

S = "scn"


def entry(value, rule="band", dir="lower", band=1.10, **kw):
    e = {"value": value, "rule": rule, "dir": dir, "reason": "test base", "since": "W0"}
    if rule == "band":
        e["band"] = band
    e.update(kw)
    return e


def baseline(keys, mode="legion", **extra):
    doc = lc.empty_baseline()
    doc["entries"] = {S: {mode: keys}}
    doc.update(extra)
    return doc


def rec(keys, mode="legion", scn=S, **kw):
    r = {"scenario": scn, "mode": mode, "ticks": 100, "units": 10, "exec": "both", "window": 1,
         "offsets": [0, 1, -1], "hash": {"0": "a", "1": "b", "-1": "c"}, "serial_eq_workers": True,
         "keys": keys, "varying": {}}
    r.update(kw)
    return r


def run_check(doc, *recs, **kw):
    table = {(r["scenario"], r["mode"]): r for r in recs}
    return lc.check(doc, table, **kw)


class Gate(unittest.TestCase):
    def test_eq_violation_fails(self):
        doc = baseline({"spins": entry(0, "eq")})
        self.assertTrue(run_check(doc, rec({"spins": 0})).ok)
        r = run_check(doc, rec({"spins": 1}))
        self.assertFalse(r.ok)
        self.assertIn("eq: expected 0, got 1", r.fails[0])

    def test_hash_and_serial_workers_are_eq(self):
        doc = baseline({"hash@0": entry("a", "eq"), "serial_eq_workers": entry(True, "eq")})
        self.assertTrue(run_check(doc, rec({})).ok)
        self.assertFalse(run_check(doc, rec({}, hash={"0": "z"})).ok)
        self.assertFalse(run_check(doc, rec({}, serial_eq_workers=False)).ok)

    def test_one_sided_loss_beyond_band_fails_lower_and_higher(self):
        doc = baseline({"stop_go": entry(1000), "g.A.arrived": entry(100, dir="higher")})
        self.assertTrue(run_check(doc, rec({"stop_go": 1100, "g.A.arrived": 91})).ok)   # inside x1.10
        r = run_check(doc, rec({"stop_go": 1101, "g.A.arrived": 90}))
        self.assertEqual(len(r.fails), 2)

    def test_time_key_never_is_the_worst_value(self):
        doc = baseline({"g.A.t90": entry(500)})
        self.assertFalse(run_check(doc, rec({"g.A.t90": -1})).ok)
        doc = baseline({"g.A.t90": entry(-1)})
        r = run_check(doc, rec({"g.A.t90": 800}))
        self.assertTrue(r.ok)
        self.assertEqual(len(r.ratchets), 1)

    def test_improvement_prints_ratchet_and_passes(self):
        doc = baseline({"stop_go": entry(1000)})
        r = run_check(doc, rec({"stop_go": 800}))
        self.assertTrue(r.ok)
        self.assertEqual(r.ratchets, [("scn/legion/stop_go", 1000, 800)])
        self.assertIn("RATCHET scn/legion/stop_go 1000 -> 800", r.lines())
        # within the band is not a ratchet
        self.assertEqual(run_check(doc, rec({"stop_go": 950})).ratchets, [])

    def test_ratchet_raises_the_reference_and_cumulative_drift_is_caught(self):
        doc = baseline({"stop_go": entry(1000)})
        r = run_check(doc, rec({"stop_go": 800}))
        lc.apply_ratchet(doc, r, "improved by test", "2026-01-01T00:00:00Z")
        self.assertEqual(doc["references"][S]["legion"]["stop_go"], 800)
        self.assertEqual(doc["history"][0]["reason"], "improved by test")
        self.assertEqual(doc["history"][0]["ratchets"][0]["new"], 800)
        # head base is still 1000 (per step ok at 950) but the reference is 800:
        # 800 x1.10 = 880 < 950, so the cumulative test fails
        r = run_check(doc, rec({"stop_go": 950}))
        self.assertFalse(r.ok)
        self.assertIn("cumulative", r.fails[0])
        self.assertTrue(run_check(doc, rec({"stop_go": 880})).ok)

    def test_missing_key_fails_but_missing_work_counter_reads_zero(self):
        doc = baseline({"flips": entry(5), "work.field_work.total": entry(0)})
        r = run_check(doc, rec({}))
        self.assertEqual(len(r.fails), 1)
        self.assertIn("flips", r.fails[0])

    def test_work_counters_and_unbaselined_classes(self):
        doc = baseline({"work.field_work.total": entry(1000)})
        self.assertTrue(run_check(doc, rec({"work.field_work.total": 1100})).ok)
        self.assertFalse(run_check(doc, rec({"work.field_work.total": 1101})).ok)
        r = run_check(doc, rec({"work.field_work.total": 1000, "work.convoy_tests.total": 7}))
        self.assertIn("unbaselined", r.fails[0])
        # a new class carries a declared per-unit bound
        doc["entries"][S]["legion"]["work.convoy_tests.total"] = entry(2, "bound", per="unit")
        self.assertTrue(run_check(doc, rec({"work.field_work.total": 1000, "work.convoy_tests.total": 20})).ok)
        self.assertFalse(run_check(doc, rec({"work.field_work.total": 1000, "work.convoy_tests.total": 21})).ok)

    def test_cumulative_combat_is_x1_00_of_the_w0_base(self):
        scn = "battle-field-2x60"
        key = "work.legion_total.max"
        doc = lc.empty_baseline()
        doc["entries"] = {scn: {"legion": {key: entry(900, w0=800)}}}
        r = rec({key: 850}, scn=scn)
        self.assertTrue(run_check(doc, r).ok)                      # step band alone: fine
        out = run_check(doc, r, cumulative=True)
        self.assertFalse(out.ok)
        self.assertIn("cumulative combat", out.fails[0])
        self.assertTrue(run_check(doc, rec({key: 800}, scn=scn), cumulative=True).ok)

    def test_reasons_test_fails_on_a_missing_reason(self):
        doc = baseline({"spins": entry(0, "eq")})
        self.assertEqual(lc.validate_baseline(doc), [])
        for section, item in (("exceptions", {"scenario": S, "key": "g.A.t90", "cluster": "MV-11"}),
                              ("accepted_regressions", {"scenario": S, "mode": "legion", "key": "flips", "max": 9})):
            bad = copy.deepcopy(doc)
            bad[section].append(item)
            self.assertTrue(any("no reason" in p for p in lc.validate_baseline(bad)), section)
            bad[section][0]["reason"] = "  "
            self.assertTrue(any("no reason" in p for p in lc.validate_baseline(bad)), section)
            bad[section][0]["reason"] = "because"
            self.assertFalse(any("no reason" in p for p in lc.validate_baseline(bad)), section)
        bad = copy.deepcopy(doc)
        del bad["entries"][S]["legion"]["spins"]["reason"]
        self.assertTrue(any("no reason" in p for p in lc.validate_baseline(bad)))
        bad = copy.deepcopy(doc)
        bad["exceptions"].append({"scenario": S, "key": "g.A.t90", "cluster": "UNASSIGNED", "reason": "r"})
        self.assertTrue(any("cluster" in p for p in lc.validate_baseline(bad)))
        bad = copy.deepcopy(doc)
        bad["accepted_regressions"].append({"scenario": S, "mode": "legion", "key": "k", "reason": "r"})
        self.assertTrue(any("max or factor" in p for p in lc.validate_baseline(bad)))
        bad = baseline({"flips": entry(3, band=1.5)})
        self.assertTrue(any("band" in p for p in lc.validate_baseline(bad)))

    def test_reasons_cli_exits_nonzero(self):
        doc = baseline({"spins": entry(0, "eq")}, exceptions=[{"scenario": S, "key": "k", "cluster": "C"}])
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "b.json")
            json.dump(doc, open(p, "w"))
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(lc.main(["validate", "--baseline", p]), 1)
                doc["exceptions"][0]["reason"] = "named"
                json.dump(doc, open(p, "w"))
                self.assertEqual(lc.main(["validate", "--baseline", p]), 0)

    # ---- the Retail floor
    def floor_doc(self, **extra):
        return baseline({"g.A.t90": entry(1000, band=1.20)}, **extra)

    def test_floor_passing_key_that_starts_failing_fails_inside_its_band(self):
        retail = rec({"g.A.t90": 1000}, mode="retail")
        ok = run_check(self.floor_doc(), rec({"g.A.t90": 1100}), retail)
        self.assertTrue(ok.ok, ok.fails)
        r = run_check(self.floor_doc(), rec({"g.A.t90": 1150}), retail)        # inside x1.20, over retail x1.10
        self.assertFalse(r.ok)
        self.assertEqual(len(r.fails), 1)
        self.assertIn("Retail floor", r.fails[0])

    def test_floor_higher_is_better_and_never(self):
        retail = rec({"g.A.arrived": 100}, mode="retail")
        doc = baseline({"g.A.arrived": entry(100, dir="higher", band=1.20)})
        self.assertTrue(run_check(doc, rec({"g.A.arrived": 91}), retail).ok)
        self.assertFalse(run_check(doc, rec({"g.A.arrived": 90}), retail).ok)
        retail = rec({"g.A.t90": -1}, mode="retail")                          # retail never finished
        self.assertTrue(run_check(baseline({"g.A.t90": entry(5000)}), rec({"g.A.t90": 5000}), retail).ok)
        retail = rec({"g.A.t90": 1000}, mode="retail")
        self.assertFalse(run_check(self.floor_doc(), rec({"g.A.t90": -1}), retail).ok)

    def test_floor_exception_may_fail_improve_and_clears(self):
        exc = [{"scenario": S, "key": "g.A.t90", "cluster": "MV-11", "reason": "known"}]
        retail = rec({"g.A.t90": 500}, mode="retail")
        doc = self.floor_doc(exceptions=exc)
        r = run_check(doc, rec({"g.A.t90": 1000}), retail)
        self.assertTrue(r.ok)                                                   # still failing, allowed
        self.assertEqual(r.exception_count, 1)
        r = run_check(doc, rec({"g.A.t90": 700}), retail)                      # improves, still above retail
        self.assertTrue(r.ok)
        self.assertEqual(r.ratchets, [("scn/legion/g.A.t90", 1000, 700)])
        self.assertEqual(r.cleared, [])
        r = run_check(doc, rec({"g.A.t90": 540}), retail)                      # now passes
        self.assertEqual(len(r.cleared), 1)
        lc.apply_ratchet(doc, r, "cleared", "2026-01-01T00:00:00Z")
        self.assertEqual(doc["exceptions"], [])
        self.assertEqual(doc["history"][0]["cleared_exceptions"], [S + "/g.A.t90"])
        # an exception still fails the outcome rule when it gets worse
        doc = self.floor_doc(exceptions=exc)
        self.assertFalse(run_check(doc, rec({"g.A.t90": 1300}), retail).ok)

    def test_ratchet_keeps_offset_spread_exceptions(self):
        exc = [{"scenario": S, "key": "g.A.t90", "cluster": "AR-11", "mode": "legion", "median5": True,
                "reason": "3-offset spread 900..1200 exceeds the band at W2s0; gated on median-of-5 offsets"}]
        retail = rec({"g.A.t90": 500}, mode="retail")
        doc = self.floor_doc(exceptions=exc)
        r = run_check(doc, rec({"g.A.t90": 540}, offsets=[0, 1, -1, 2, -2]), retail)  # passes the floor
        self.assertEqual(len(r.cleared), 1)
        lc.apply_ratchet(doc, r, "spread stays", "2026-01-01T00:00:00Z")
        self.assertEqual(doc["exceptions"], exc)
        self.assertEqual(doc["history"][0]["cleared_exceptions"], [])

    def test_median5_exception_needs_five_offsets(self):
        exc = [{"scenario": S, "key": "g.A.t90", "cluster": "MV-11", "reason": "noisy", "median5": True}]
        retail = rec({"g.A.t90": 500}, mode="retail")
        doc = self.floor_doc(exceptions=exc)
        self.assertFalse(run_check(doc, rec({"g.A.t90": 1000}), retail).ok)
        five = rec({"g.A.t90": 1000}, offsets=[0, 1, -1, 2, -2])
        self.assertTrue(run_check(doc, five, retail).ok)

    def test_spread_beyond_band_must_be_an_exception(self):
        doc = baseline({"flips": entry(100)})
        wide = rec({"flips": 100}, varying={"flips": [100, 140, 100]})
        r = run_check(doc, wide)
        self.assertFalse(r.ok)
        self.assertIn("spread", r.fails[0])
        doc["exceptions"].append({"scenario": S, "key": "flips", "cluster": "C", "reason": "noisy"})
        self.assertTrue(run_check(doc, wide).ok)
        self.assertTrue(run_check(baseline({"flips": entry(100)}),
                                  rec({"flips": 100}, varying={"flips": [100, 105, 100]})).ok)

    def test_retake_lists_spread_keys_as_median5_exceptions(self):
        doc = lc.empty_baseline()
        run = {(S, "legion"): rec({"flips": 100, "stable": 5}, varying={"flips": [100, 140, 100]})}
        lc.write_base(doc, run, "step 0", "W0", ["*"], False, [])
        self.assertEqual([(e["key"], e["median5"], e["cluster"]) for e in doc["exceptions"]],
                         [("flips", True, "offset-spread")])
        self.assertEqual(lc.validate_baseline(doc), [])
        table = dict(run)
        self.assertFalse(lc.check(doc, table).ok)                               # three offsets are not enough
        table[(S, "legion")] = rec({"flips": 100, "stable": 5}, varying={"flips": [100, 140, 100]},
                                   offsets=[0, 1, -1, 2, -2])
        self.assertTrue(lc.check(doc, table).ok)

    # ---- declared tolerances
    def tol_doc(self, **tol):
        a = {"scenario": S, "mode": "legion", "key": "spins_row", "step": "W5-MV06", "factor": 1.05,
             "reason": "declared tolerance"}
        a.update(tol)
        return baseline({"spins_row": entry(1000, band=1.0), "other": entry(1000, band=1.0)},
                        accepted_regressions=[a])

    def test_tolerance_licenses_only_its_step_and_key(self):
        doc = self.tol_doc()
        keys = {"spins_row": 1040, "other": 1000}
        r = run_check(doc, rec(keys), step="W5-MV06")
        self.assertTrue(r.ok, r.fails)
        self.assertEqual(len(r.licensed), 1)
        self.assertFalse(run_check(doc, rec(keys), step="W6-X").ok)            # another step
        self.assertFalse(run_check(doc, rec(keys)).ok)                         # no step named
        self.assertFalse(run_check(doc, rec({"spins_row": 1000, "other": 1040}), step="W5-MV06").ok)  # other key
        self.assertFalse(run_check(doc, rec({"spins_row": 1060, "other": 1000}), step="W5-MV06").ok)  # beyond x1.05

    def test_permanent_accepted_regression_with_absolute_max(self):
        doc = self.tol_doc(step=None, max=1200)
        del doc["accepted_regressions"][0]["step"], doc["accepted_regressions"][0]["factor"]
        self.assertTrue(run_check(doc, rec({"spins_row": 1200, "other": 1000})).ok)
        self.assertFalse(run_check(doc, rec({"spins_row": 1201, "other": 1000})).ok)

    def test_tolerance_never_licenses_a_floor_failure(self):
        doc = baseline({"g.A.t90": entry(1000, band=1.0)},
                       accepted_regressions=[{"scenario": S, "mode": "legion", "key": "g.A.t90", "step": "W5",
                                              "factor": 1.2, "reason": "declared"}])
        retail = rec({"g.A.t90": 1000}, mode="retail")
        self.assertTrue(run_check(doc, rec({"g.A.t90": 1100}), retail, step="W5").ok)
        r = run_check(doc, rec({"g.A.t90": 1150}), retail, step="W5")           # licensed by the tolerance, over the floor
        self.assertFalse(r.ok)
        self.assertEqual(len(r.licensed), 1)
        self.assertIn("Retail floor", r.fails[0])

    def test_missing_scenario_is_skipped_unless_required(self):
        doc = baseline({"flips": entry(1)})
        self.assertTrue(lc.check(doc, {}).ok)
        self.assertFalse(lc.check(doc, {}, require_all=True).ok)
        self.assertTrue(lc.check(doc, {}, skipped=[S], require_all=False).ok)


class Cli(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.TemporaryDirectory()
        self.addCleanup(self.d.cleanup)

    def path(self, name):
        return os.path.join(self.d.name, name)

    def write_run(self, name, *recs):
        with open(self.path(name), "w") as f:
            for r in recs:
                f.write(json.dumps(r) + "\n")
        return self.path(name)

    def call(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = lc.main(list(argv))
        return code, out.getvalue()

    def test_baseline_retake_check_and_ratchet_end_to_end(self):
        run = self.write_run("run.jsonl",
                             rec({"spins": 0, "g.A.t90": 900, "g.A.arrived": 40, "work.field_work.total": 500}),
                             rec({"spins": 4, "g.A.t90": 400, "g.A.arrived": 40}, mode="retail"))
        b = self.path("baseline.json")
        code, out = self.call("baseline", "--baseline", b, "--results", run, "--reason", "first base",
                              "--hashes", "--cluster", "scn/g.A.t90=MV-11")
        self.assertEqual(code, 0, out)
        doc = json.load(open(b))
        self.assertEqual(doc["entries"][S]["legion"]["spins"]["rule"], "eq")
        self.assertEqual(doc["entries"][S]["legion"]["hash@0"]["value"], "a")
        self.assertEqual(doc["entries"][S]["legion"]["g.A.arrived"]["dir"], "higher")
        self.assertEqual([(e["key"], e["cluster"]) for e in doc["exceptions"]], [("g.A.t90", "MV-11")])
        code, out = self.call("check", "--baseline", b, "--results", run)
        self.assertEqual(code, 0, out)
        self.assertIn("PASS", out)
        # an improvement: RATCHET printed, exit 0, nothing written without --ratchet
        better = self.write_run("better.jsonl",
                                rec({"spins": 0, "g.A.t90": 600, "g.A.arrived": 40, "work.field_work.total": 500}),
                                rec({"spins": 4, "g.A.t90": 400, "g.A.arrived": 40}, mode="retail"))
        code, out = self.call("check", "--baseline", b, "--results", better)
        self.assertEqual(code, 0, out)
        self.assertIn("RATCHET scn/legion/g.A.t90 900 -> 600", out)
        self.assertEqual(json.load(open(b)).get("history"), [])
        code, out = self.call("check", "--baseline", b, "--results", better, "--ratchet", "t90 faster",
                              "--date", "2026-10-08T00:00:00Z")
        self.assertEqual(code, 0, out)
        doc = json.load(open(b))
        self.assertEqual(doc["references"][S]["legion"]["g.A.t90"], 600)
        self.assertEqual(doc["history"][0]["when"], "2026-10-08T00:00:00Z")
        self.assertEqual(doc["history"][0]["reason"], "t90 faster")
        # a failing run does not ratchet
        worse_run = self.write_run("worse.jsonl", rec({"spins": 2, "g.A.t90": 600, "g.A.arrived": 40}),
                                   rec({"spins": 4, "g.A.t90": 400, "g.A.arrived": 40}, mode="retail"))
        code, out = self.call("check", "--baseline", b, "--results", worse_run, "--ratchet", "no")
        self.assertEqual(code, 1)
        self.assertIn("not ratcheting", out)
        self.assertEqual(len(json.load(open(b))["history"]), 1)

    def test_exit_table_marks_intended_and_incidental(self):
        old = self.write_run("old.jsonl", rec({"a": 100, "b": 100, "c": 100, "d": 0}))
        new = self.write_run("new.jsonl", rec({"a": 50, "b": 104, "c": 120, "d": 3}))
        code, out = self.call("exit-table", "--baseline", self.path("none.json"), "--results", new,
                              "--base", old, "--intended", "scn/legion/a")
        self.assertEqual(code, 0)
        self.assertIn("| scn/legion/a | 100 | 50 | -50.0% | intended |", out)
        self.assertIn("| scn/legion/c | 100 | 120 | +20.0% | incidental (within band) |", out)
        self.assertIn("| scn/legion/d | 0 | 3 | new | incidental (within band) |", out)
        self.assertNotIn("scn/legion/b", out)                                  # 4% is not a move
        self.assertIn("3 keys moved by more than 5%; 1 intended, 2 incidental", out)

    def test_exit_table_reads_a_spread_bound_at_its_median(self):
        base = self.path("base.json")
        json.dump({"entries": {S: {"legion": {
            "a": {"rule": "bound", "dir": "lower", "value": 663,
                  "reason": "W0: offset spread 1.21 exceeds band 1.20 ...: one-sided bound at median 552 x1.20"},
            "b": {"rule": "bound", "dir": "lower", "value": 36, "per": "unit", "reason": "declared bound"},
            "c": {"rule": "band", "dir": "lower", "value": 100, "reason": "r"}}}}}, open(base, "w"))
        new = self.write_run("new.jsonl", rec({"a": 552, "b": 9, "c": 130}))
        code, out = self.call("exit-table", "--baseline", self.path("none.json"), "--results", new, "--base", base)
        self.assertEqual(code, 0)
        self.assertNotIn("scn/legion/a", out)                                  # median 552 -> 552
        self.assertNotIn("scn/legion/b", out)                                  # a limit, not a base
        self.assertIn("| scn/legion/c | 100 | 130 | +30.0% |", out)
        self.assertIn("1 keys moved by more than 5%", out)

    def test_anchor_diff_prints_cumulative_drift_and_never_gates(self):
        anchor = self.path("anchor.json")
        json.dump({"entries": {S: {"legion": {"flips": 100, "g.A.arrived": 50, "same": 7}}}}, open(anchor, "w"))
        run = self.write_run("run.jsonl", rec({"flips": 130, "g.A.arrived": 60, "same": 7}))
        code, out = self.call("anchor", "--anchor", anchor, "--results", run)
        self.assertEqual(code, 0)
        self.assertIn("scn/legion/flips", out)
        self.assertIn("(+30.0%) worse", out)
        self.assertIn("(+20.0%) better", out)
        self.assertIn("2 of 3 common keys differ from the anchor; 1 worse, 1 better", out)

    def test_committed_baseline_is_valid(self):
        p = os.path.join(HERE, "scenarios", "baseline.json")
        if not os.path.exists(p):
            self.skipTest("no committed baseline")
        self.assertEqual(lc.validate_baseline(json.load(open(p))), [])


def main():
    path = None
    argv = sys.argv[1:]
    if argv and not argv[0].startswith("-"):
        path = argv.pop(0)
    if path is None:
        path = os.path.join(HERE, "scenarios", "baseline.json")
    bad = lc.validate_baseline(lc.load_json(path)) if os.path.exists(path) else ["no baseline at " + path]
    for b in bad:
        print("FAIL " + b)
    print("%s: %d baseline problems in %s" % ("FAIL" if bad else "ok", len(bad), path))
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    res = unittest.TextTestRunner(verbosity=1).run(suite)
    return 0 if not bad and res.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
