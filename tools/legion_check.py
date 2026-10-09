#!/usr/bin/env python3
"""legion_check -- the Legion baseline gate (PLAN 3.0 / 3.8, T8-synthesis (5)).

All of the gating logic lives here. `legion_scenario --check B.json FILE.scn...`
runs the scenarios and hands their JSON lines to this script; the same lines
(`legion_scenario ... --json > run.jsonl`) can be checked offline.

  legion_check.py check      --baseline B --results RUN.jsonl... [--step ID]
                             [--cumulative] [--require-all] [--ratchet REASON]
  legion_check.py baseline   --baseline B --results RUN.jsonl... --reason TEXT
                             [--since ID] [--keys GLOB]... [--hashes]
                             [--cluster GLOB=CLUSTER]...      (step 0: retake the base)
  legion_check.py exit-table --baseline B --results RUN.jsonl... --base OLD
                             [--intended GLOB]... [--intended-file F]
  legion_check.py anchor     --anchor ANCHOR.json --results RUN.jsonl...
  legion_check.py validate   --baseline B                      (the reasons ctest)

Exit: 0 pass, 1 a gate failed, 2 usage / tooling error.

baseline.json
  {"version": 1,
   "entries": {SCENARIO: {MODE: {KEY: {value, dir, rule, band, reason, since,
                                       [per: "unit"], [w0: number]}}}},
   "references": {SCENARIO: {MODE: {KEY: number}}},     # W0 base raised by ratchets
   "exceptions": [{scenario, key, cluster, reason, [mode], [median5: true]}],
   "accepted_regressions": [{scenario, mode, key, reason, [step], max | factor}],
   "history": [{when, reason, ratchets: [...], cleared_exceptions: [...]}]}
  dir     lower | higher  (which direction is better)
  rule    eq     the value must equal `value` (safety keys, hashes, serial==workers)
          band   one-sided: worse than `value` x band (per step) or than the
                 reference x band (cumulative) fails; better than the reference
                 by more than the band prints RATCHET and passes
          bound  an absolute declared bound (`per: unit` divides by the units)
  Time keys (.t50 .t90 .done .orders_done): -1 reads "never" = infinitely bad.
  KEYs: observer / work.* keys of the runner's "keys", `hash@OFFSET`, and
        `serial_eq_workers`.

Precedence (PLAN 3.0): eq/safety, then the Retail floor, then a declared
tolerance or accepted regression, then the bands. A tolerance never licenses a
Retail-floor failure; one with a `step` licenses its loss only when `--step`
names that step, and only up to its `max` (absolute) or `factor` (x the base).
"""
import argparse
import datetime
import fnmatch
import json
import math
import re
import sys

INF = float("inf")
BAND = 1.10
FLOOR = 1.10
MAX_BAND = 1.20
TIME_SUFFIXES = (".t50", ".t90", ".done", ".orders_done")
COMBAT_SCENARIOS = ("battle-field*", "battle-assault*")
COMBAT_KEYS = ("work.legion_total.total", "work.legion_total.max", "work.legion_total.p99")
SKIP_KEYS = ("members", "samples", "commands", "last_command_tick", "decision_samples")
BAD_CLUSTERS = ("", "UNASSIGNED", "TODO", "?")


# ---------------------------------------------------------------- key classes
def is_time(key):
    return key.endswith(TIME_SUFFIXES)


def default_dir(key):
    if key.endswith((".arrived", ".flyers_landed", ".inside", ".complete_n", "approach.arrived_n")):
        return "higher"
    return "lower"


def floor_dir(key):
    """Direction if `key` is a Retail-floor key (PLAN 3.0), else None."""
    if key.endswith(".arrived"):
        return "higher"
    if (key.endswith(".t90") or key in ("spins", "parked_no_progress") or key.endswith(".crossings")
            or key.startswith("wall_touch") or (key.startswith("contact_") and key.endswith("_permille"))):
        return "lower"
    return None


def norm(key, x):
    """Comparable number; `never` (-1 on a time key) is infinitely bad."""
    if isinstance(x, bool) or not isinstance(x, (int, float)):
        return x
    if is_time(key) and x < 0:
        return INF
    return x


def worse(lower, cur, ref, band):
    """cur is worse than ref by more than band (one-sided)."""
    if lower:
        return cur > ref * band + 1e-9 if ref != INF else False
    return cur * band + 1e-9 < ref


def better(lower, cur, ref, band):
    """cur is better than ref by more than band."""
    if lower:
        return cur * band + 1e-9 < ref
    return cur > ref * band + 1e-9 if ref != INF else False


def fmt(x):
    if x == INF:
        return "never"
    if isinstance(x, float):
        return "%.4g" % x
    return str(x)


# --------------------------------------------------------------------- loading
def load_json(path):
    with open(path) as f:
        return json.load(f)


def load_results(paths):
    """JSON lines from the runner -> {(scenario, mode): record}; skipped lines
    are returned apart."""
    recs, skipped = {}, []
    for p in paths:
        lines = sys.stdin.read().splitlines() if p == "-" else open(p).read().splitlines()
        for ln in lines:
            ln = ln.strip()
            if not ln.startswith("{"):
                continue
            r = json.loads(ln)
            if "skipped" in r:
                skipped.append(r["scenario"])
                continue
            recs[(r["scenario"], r["mode"])] = r
    return recs, skipped


def lookup(rec, key):
    """(found, value) of a baseline key in a result record."""
    if key.startswith("hash@"):
        v = rec.get("hash", {}).get(key[5:])
        return v is not None, v
    if key == "serial_eq_workers":
        return "serial_eq_workers" in rec, rec.get("serial_eq_workers")
    keys = rec.get("keys", {})
    if key in keys:
        return True, keys[key]
    if key.startswith("work."):
        return True, 0           # the runner counts a missing work.* key as 0
    return False, None


def empty_baseline():
    return {"version": 1, "entries": {}, "references": {}, "exceptions": [],
            "accepted_regressions": [], "history": []}


def flatten(path):
    """{(scenario, mode, key): number} from a baseline (values), an anchor file
    ({"entries": {s: {m: {k: number|{value}}}}}) or runner JSON lines."""
    try:
        doc = load_json(path)
    except ValueError:
        doc = None
    out = {}
    if isinstance(doc, dict) and "entries" in doc:
        for s, modes in doc["entries"].items():
            for m, keys in modes.items():
                for k, e in keys.items():
                    v = e.get("value") if isinstance(e, dict) else e
                    if isinstance(e, dict) and e.get("rule") == "bound":
                        # A bound's value is a limit, not a measurement: a W0 spread bound
                        # stores its median x band ("bound at median N x1.20"), so the move is
                        # read against N; a declared bound (per unit, ...) has no base value.
                        mm = re.search(r"at median (-?[0-9.]+)", e.get("reason", ""))
                        if not mm:
                            continue
                        v = float(mm.group(1))
                        v = int(v) if v == int(v) else v
                    if isinstance(v, (int, float)) and not isinstance(v, bool):
                        out[(s, m, k)] = v
        return out
    recs, _ = load_results([path])
    for (s, m), r in recs.items():
        for k, v in r.get("keys", {}).items():
            out[(s, m, k)] = v
    return out


# ------------------------------------------------------------------ validation
def validate_baseline(doc):
    """Problems (strings) in a baseline document; empty = valid. The reasons
    ctest fails on any."""
    bad = []
    if not isinstance(doc, dict) or doc.get("version") != 1:
        return ["baseline: version must be 1"]

    def need_reason(where, e):
        if not isinstance(e.get("reason"), str) or not e["reason"].strip():
            bad.append("%s: no reason" % where)

    for s, modes in doc.get("entries", {}).items():
        for m, keys in modes.items():
            for k, e in keys.items():
                w = "entries %s/%s/%s" % (s, m, k)
                need_reason(w, e)
                if e.get("rule") not in ("eq", "band", "bound"):
                    bad.append("%s: rule must be eq|band|bound" % w)
                if e.get("rule") != "eq" and e.get("dir") not in ("lower", "higher"):
                    bad.append("%s: dir must be lower|higher" % w)
                if "value" not in e:
                    bad.append("%s: no value" % w)
                b = e.get("band", BAND)
                if e.get("rule") == "band" and not (1.0 <= b <= MAX_BAND):
                    bad.append("%s: band %s outside 1.0..%s" % (w, b, MAX_BAND))
    for i, e in enumerate(doc.get("exceptions", [])):
        w = "exceptions[%d] %s/%s" % (i, e.get("scenario"), e.get("key"))
        need_reason(w, e)
        if str(e.get("cluster", "")).strip() in BAD_CLUSTERS:
            bad.append("%s: no cluster id" % w)
        if not e.get("scenario") or not e.get("key"):
            bad.append("%s: scenario and key required" % w)
    for i, e in enumerate(doc.get("accepted_regressions", [])):
        w = "accepted_regressions[%d] %s/%s/%s" % (i, e.get("scenario"), e.get("mode"), e.get("key"))
        need_reason(w, e)
        if not e.get("scenario") or not e.get("key") or not e.get("mode"):
            bad.append("%s: scenario, mode and key required" % w)
        if "max" not in e and "factor" not in e:
            bad.append("%s: needs max or factor (a loss without a limit licenses anything)" % w)
    for i, e in enumerate(doc.get("history", [])):
        need_reason("history[%d]" % i, e)
    return bad


# ------------------------------------------------------------------------ check
class Report:
    def __init__(self):
        self.fails, self.ratchets, self.info, self.licensed = [], [], [], []
        self.cleared, self.exception_count = [], 0
        self.checked = 0
        self.m5 = set()

    def fail(self, path, msg):
        self.fails.append("%s: %s" % (path, msg))

    @property
    def ok(self):
        return not self.fails

    def lines(self):
        out = ["FAIL %s" % f for f in self.fails]
        out += ["RATCHET %s %s -> %s" % (p, fmt(o), fmt(n)) for p, o, n in self.ratchets]
        out += ["LICENSED %s" % l for l in self.licensed]
        out += ["CLEARED exception %s (passes the Retail floor again; removed at the next ratchet)" % c
                for c in self.cleared]
        out += ["INFO %s" % i for i in self.info]
        out.append("%s: %d keys checked, %d failed, %d ratchets, %d licensed, %d floor exceptions"
                   % ("PASS" if self.ok else "FAIL", self.checked, len(self.fails), len(self.ratchets),
                      len(self.licensed), self.exception_count))
        return out


def find_license(doc, scn, mode, key, step):
    for a in doc.get("accepted_regressions", []):
        if not (fnmatch.fnmatch(scn, a.get("scenario", "")) and fnmatch.fnmatch(mode, a.get("mode", ""))
                and fnmatch.fnmatch(key, a.get("key", ""))):
            continue
        if a.get("step") is not None and a["step"] != step:
            continue
        yield a


def licensed_by(doc, scn, mode, key, step, lower, cur, base, ref):
    for a in find_license(doc, scn, mode, key, step):
        if "max" in a:
            lim = norm(key, a["max"])
            ok = cur <= lim if lower else cur >= lim
        else:
            f = a["factor"]
            anchor = max(base, ref) if lower else min(base, ref)
            ok = (cur <= anchor * f + 1e-9) if lower else (cur * f + 1e-9 >= anchor)
        if ok:
            return a
    return None


def judge(doc, rpt, scn, mode, key, e, rec, step):
    path = "%s/%s/%s" % (scn, mode, key)
    found, raw = lookup(rec, key)
    if not found:
        rpt.fail(path, "key missing from the run")
        return
    rpt.checked += 1
    rule = e.get("rule", "band")
    if rule == "eq":
        if raw != e["value"]:
            rpt.fail(path, "eq: expected %s, got %s" % (e["value"], raw))
        return
    lower = e.get("dir", default_dir(key)) == "lower"
    cur = norm(key, raw)
    base = norm(key, e["value"])
    if rule == "bound":
        if e.get("per") == "unit":
            cur = cur / max(1, rec.get("units", 1))
        if (cur > base + 1e-9) if lower else (cur + 1e-9 < base):
            rpt.fail(path, "bound %s %s, got %s%s" % ("<=" if lower else ">=", fmt(base), fmt(cur),
                                                      " per unit" if e.get("per") == "unit" else ""))
        return
    band = e.get("band", BAND)
    refraw = doc.get("references", {}).get(scn, {}).get(mode, {}).get(key, e["value"])
    ref = norm(key, refraw)
    step_bad = worse(lower, cur, base, band)
    cum_bad = worse(lower, cur, ref, band)
    if step_bad or cum_bad:
        lic = licensed_by(doc, scn, mode, key, step, lower, cur, base, ref)
        if lic:
            rpt.licensed.append("%s %s vs base %s / reference %s: %s" % (path, fmt(cur), fmt(base), fmt(ref),
                                                                       lic["reason"]))
            return
        which = []
        if step_bad:
            which.append("per-step vs head base %s x%.2f" % (fmt(base), band))
        if cum_bad:
            which.append("cumulative vs reference %s x%.2f" % (fmt(ref), band))
        rpt.fail(path, "%s than allowed (%s), got %s" % ("higher" if lower else "lower", "; ".join(which), fmt(cur)))
    elif better(lower, cur, ref, band):
        rpt.ratchets.append((path, ref, cur))


def spread_of(key, vals):
    """(lo, hi, hi/lo) of a key's per-offset values; the ratio is inf when lo is 0 or never."""
    v = [norm(key, x) for x in vals]
    lo, hi = min(v), max(v)
    if hi == lo:
        return lo, hi, 1.0
    return lo, hi, INF if lo <= 0 or hi == INF else hi / lo


def spread_check(doc, rpt, scn, mode, key, e, rec, exc):
    vals = rec.get("varying", {}).get(key)
    if not vals or key.startswith("work.") or e.get("rule") != "band":
        return
    band = e.get("band", BAND)
    lo, hi, ratio = spread_of(key, vals)
    if ratio <= band:
        return
    if exc is None:
        rpt.fail("%s/%s/%s" % (scn, mode, key),
                 "offset spread %s..%s exceeds band %.2f: list it under exceptions (cluster, reason) and gate "
                 "it on median-of-5" % (fmt(lo), fmt(hi), band))


def floor_passes(lower, legion, retail):
    """legion <= retail x1.1 (lower is better) or legion x1.1 >= retail."""
    return not worse(True, legion, retail, FLOOR) if lower else legion * FLOOR + 1e-9 >= retail


def need_median5(rpt, path, exc, rec):
    if exc.get("median5") and len(rec.get("offsets", [])) < 5 and path not in rpt.m5:
        rpt.m5.add(path)
        rpt.fail(path, "exception is gated on median-of-5 offsets; run has %d (pass --offsets with 5 values)"
                 % len(rec.get("offsets", [])))


def exception_for(doc, scn, mode, key):
    return next((x for x in doc.get("exceptions", [])
                 if x["scenario"] == scn and x["key"] == key and x.get("mode", mode) == mode), None)


def floor_check(doc, rpt, recs):
    for (scn, mode), lrec in sorted(recs.items()):
        if mode != "legion" or (scn, "retail") not in recs:
            continue
        rrec = recs[(scn, "retail")]
        for key, lv in sorted(lrec.get("keys", {}).items()):
            fd = floor_dir(key)
            if fd is None or key not in rrec.get("keys", {}):
                continue
            lower = fd == "lower"
            lo, re_ = norm(key, lv), norm(key, rrec["keys"][key])
            passes = floor_passes(lower, lo, re_)
            exc = exception_for(doc, scn, "legion", key)
            path = "%s/legion/%s" % (scn, key)
            if exc:
                rpt.exception_count += 1
                if passes:
                    rpt.cleared.append("%s (legion %s vs retail %s, %s)" % (path, fmt(lo), fmt(re_), exc["cluster"]))
                else:
                    rpt.info.append("floor exception %s legion %s vs retail %s (%s)" % (path, fmt(lo), fmt(re_),
                                                                                       exc["cluster"]))
                need_median5(rpt, path, exc, lrec)
            elif not passes:
                rpt.fail(path, "Retail floor: legion %s vs retail %s (needs %s); a passing key may not start "
                               "failing the floor" % (fmt(lo), fmt(re_), "legion <= retail x%.1f" % FLOOR if lower
                                                      else "legion x%.1f >= retail" % FLOOR))


def check(doc, recs, skipped=(), step=None, cumulative=False, require_all=False):
    rpt = Report()
    entries = doc.get("entries", {})
    for scn, modes in sorted(entries.items()):
        for mode, keys in sorted(modes.items()):
            rec = recs.get((scn, mode))
            if rec is None:
                if scn in skipped:
                    rpt.info.append("%s/%s skipped by the runner (needs data)" % (scn, mode))
                elif require_all:
                    rpt.fail("%s/%s" % (scn, mode), "no result in the run")
                continue
            for key, e in sorted(keys.items()):
                exc = exception_for(doc, scn, mode, key)
                if exc:
                    need_median5(rpt, "%s/%s/%s" % (scn, mode, key), exc, rec)
                judge(doc, rpt, scn, mode, key, e, rec, step)
                spread_check(doc, rpt, scn, mode, key, e, rec, exc)
                if cumulative and key in COMBAT_KEYS and any(fnmatch.fnmatch(scn, g) for g in COMBAT_SCENARIOS):
                    w0 = e.get("w0")
                    w0 = e["value"] if isinstance(w0, bool) or w0 is None else w0
                    found, cur = lookup(rec, key)
                    if found and cur > w0:
                        rpt.fail("%s/%s/%s" % (scn, mode, key),
                                 "cumulative combat cost: %s above the W0 base %s (x1.00, PLAN 3.0)" % (cur, w0))
            # a work counter that moved with no entry has no base to compare with
            for key, v in sorted(rec.get("keys", {}).items()):
                if key.startswith("work.") and v and key not in keys:
                    rpt.fail("%s/%s/%s" % (scn, mode, key),
                             "unbaselined work counter %s: a step that adds a class declares its per-unit bound" % v)
    for scn, mode in sorted(recs):
        if scn not in entries or mode not in entries[scn]:
            rpt.info.append("%s/%s has no baseline entries" % (scn, mode))
    floor_check(doc, rpt, recs)
    return rpt


def is_spread_exception(x):
    """An exception written for a key's offset spread (write_base / step-0 retakes), not for
    the Retail floor."""
    return bool(x.get("median5")) and "offset spread" in x.get("reason", "")[:24]


def apply_ratchet(doc, rpt, reason, when):
    refs = doc.setdefault("references", {})
    moved = []
    for path, old, new in rpt.ratchets:
        scn, mode, key = path.split("/", 2)
        refs.setdefault(scn, {}).setdefault(mode, {})[key] = -1 if new == INF else new
        moved.append({"key": path, "old": -1 if old == INF else old, "new": -1 if new == INF else new})
    cleared = []
    keep = []
    for x in doc.get("exceptions", []):
        tag = "%s/legion/%s " % (x["scenario"], x["key"])
        # An offset-spread exception (median-of-5 gating) is not a floor exception: the key's
        # spread does not go away when its median passes the Retail floor, so it is kept.
        if is_spread_exception(x):
            keep.append(x)
            continue
        if any(c.startswith(tag) for c in rpt.cleared):
            cleared.append("%s/%s" % (x["scenario"], x["key"]))
        else:
            keep.append(x)
    doc["exceptions"] = keep
    doc.setdefault("history", []).append({"when": when, "reason": reason, "ratchets": moved,
                                          "cleared_exceptions": cleared})
    return len(moved), len(cleared)


# ----------------------------------------------------------- baseline retake
def write_base(doc, recs, reason, since, patterns, hashes, clusters):
    """Step 0: take every (matching) key of the run as the head's base. Existing
    entries keep their rule, dir, band and reference; new ones get class
    defaults. Floor failures that are not yet exceptions are added with their
    cluster (`--cluster GLOB=CLUSTER`, default UNASSIGNED, which the reasons
    test rejects)."""
    entries = doc.setdefault("entries", {})
    added = changed = 0
    for (scn, mode), rec in sorted(recs.items()):
        slot = entries.setdefault(scn, {}).setdefault(mode, {})
        todo = {k: v for k, v in rec.get("keys", {}).items()
                if k not in SKIP_KEYS and any(fnmatch.fnmatch(k, p) for p in patterns)}
        if hashes:
            for off, h in rec.get("hash", {}).items():
                todo["hash@" + off] = h
            if rec.get("serial_eq_workers") is not None:
                todo["serial_eq_workers"] = rec["serial_eq_workers"]
        for k, v in todo.items():
            e = slot.get(k)
            if e is None:
                if k.startswith("hash@") or k == "serial_eq_workers":
                    e = {"rule": "eq", "dir": "lower"}
                elif k.startswith("work."):
                    e = {"rule": "band", "dir": "lower", "band": BAND}
                elif k == "spins" and v == 0 or k.endswith("illegal_overlap_ticks") and v == 0:
                    e = {"rule": "eq", "dir": "lower"}
                else:
                    e = {"rule": "band", "dir": default_dir(k), "band": BAND}
                e["reason"] = reason
                e["since"] = since
                slot[k] = e
                added += 1
            elif e.get("value") != v:
                changed += 1
            e["value"] = v
        for k, vals in rec.get("varying", {}).items():
            e = slot.get(k)
            if e is None or e.get("rule") != "band" or k.startswith("work."):
                continue
            lo, hi, ratio = spread_of(k, vals)
            if ratio <= e.get("band", BAND):
                continue
            exc = exception_for(doc, scn, mode, k)
            if exc is None:
                cluster = next((c for g, c in clusters if fnmatch.fnmatch("%s/%s" % (scn, k), g)), "offset-spread")
                doc.setdefault("exceptions", []).append(
                    {"scenario": scn, "key": k, "mode": mode, "cluster": cluster, "median5": True,
                     "reason": "3-offset spread %s..%s exceeds the band at %s; gated on median-of-5 offsets"
                               % (fmt(lo), fmt(hi), since)})
            else:
                exc["median5"] = True
        if mode == "legion" and (scn, "retail") in recs:
            for k, v in rec.get("keys", {}).items():
                fd = floor_dir(k)
                rv = recs[(scn, "retail")].get("keys", {}).get(k)
                if fd is None or rv is None:
                    continue
                lo, rr = norm(k, v), norm(k, rv)
                ok = floor_passes(fd == "lower", lo, rr)
                if ok or exception_for(doc, scn, "legion", k):
                    continue
                cluster = next((c for g, c in clusters if fnmatch.fnmatch("%s/%s" % (scn, k), g)), "UNASSIGNED")
                doc["exceptions"].append({"scenario": scn, "key": k, "cluster": cluster,
                                          "reason": "fails legion <= retail x%.1f at %s: legion %s vs retail %s"
                                                    % (FLOOR, since, fmt(lo), fmt(rr))})
    return added, changed


# ----------------------------------------------------- exit table / anchor diff
def pct(old, new):
    if old == new:
        return 0.0
    if old == 0:
        return INF
    return (new - old) / abs(old) * 100.0


def exit_table(old, new, intended):
    rows = []
    for k in sorted(set(old) & set(new)):
        o, n = old[k], new[k]
        p = pct(o, n)
        if abs(p) > 5.0:
            path = "%s/%s/%s" % k
            mark = "intended" if any(fnmatch.fnmatch(path, g) for g in intended) else "incidental (within band)"
            rows.append((path, o, n, p, mark))
    return rows


def run_exit_table(args, recs):
    old = flatten(args.base)
    new = {}
    for (s, m), r in recs.items():
        for k, v in r.get("keys", {}).items():
            new[(s, m, k)] = v
    intended = list(args.intended)
    if args.intended_file:
        intended += [l.strip() for l in open(args.intended_file) if l.strip() and not l.startswith("#")]
    rows = exit_table(old, new, intended)
    print("| key | old | new | change | kind |")
    print("|---|---|---|---|---|")
    for path, o, n, p, mark in rows:
        print("| %s | %s | %s | %s | %s |" % (path, o, n, "new" if p == INF else "%+.1f%%" % p, mark))
    print("%d keys moved by more than 5%%; %d intended, %d incidental"
          % (len(rows), sum(r[4] == "intended" for r in rows), sum(r[4] != "intended" for r in rows)))
    return 0


def run_anchor(args, recs):
    anchor = flatten(args.anchor)
    cur = {}
    for (s, m), r in recs.items():
        for k, v in r.get("keys", {}).items():
            cur[(s, m, k)] = v
    drift = []
    for k in sorted(set(anchor) & set(cur)):
        a, c = anchor[k], cur[k]
        if a == c:
            continue
        lower = default_dir(k[2]) == "lower"
        p = pct(a, c)
        worseish = (c > a) == lower
        drift.append(("%s/%s/%s" % k, a, c, p, worseish))
    drift.sort(key=lambda d: -(abs(d[3]) if d[3] != INF else 1e18))
    print("anchor drift (frozen %s; report only, never gates)" % args.anchor)
    for path, a, c, p, w in drift:
        print("  %-60s %s -> %s  (%s) %s" % (path, a, c, "new" if p == INF else "%+.1f%%" % p,
                                              "worse" if w else "better"))
    print("%d of %d common keys differ from the anchor; %d worse, %d better"
          % (len(drift), len(set(anchor) & set(cur)), sum(d[4] for d in drift), sum(not d[4] for d in drift)))
    return 0


# --------------------------------------------------------------------------- cli
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("check", "baseline", "exit-table", "anchor", "validate"))
    ap.add_argument("--baseline")
    ap.add_argument("--results", nargs="+", default=[])
    ap.add_argument("--step")
    ap.add_argument("--cumulative", action="store_true")
    ap.add_argument("--require-all", action="store_true")
    ap.add_argument("--ratchet", metavar="REASON")
    ap.add_argument("--date", help="history timestamp (default now; tests pin it)")
    ap.add_argument("--reason")
    ap.add_argument("--since", default="W0")
    ap.add_argument("--keys", action="append", default=None)
    ap.add_argument("--hashes", action="store_true")
    ap.add_argument("--cluster", action="append", default=[])
    ap.add_argument("--base")
    ap.add_argument("--intended", action="append", default=[])
    ap.add_argument("--intended-file")
    ap.add_argument("--anchor")
    args = ap.parse_args(argv)

    def need(v, name):
        if not v:
            print("legion_check: %s needs %s" % (args.cmd, name), file=sys.stderr)
            sys.exit(2)

    if args.cmd == "anchor":
        need(args.anchor, "--anchor")
        recs, _ = load_results(args.results)
        return run_anchor(args, recs)
    if args.cmd != "exit-table":
        need(args.baseline, "--baseline")
    if args.cmd == "validate":
        doc = load_json(args.baseline)
        bad = validate_baseline(doc)
        for b in bad:
            print("FAIL " + b)
        print("%s: %d problems" % ("PASS" if not bad else "FAIL", len(bad)))
        return 1 if bad else 0
    need(args.results, "--results")
    recs, skipped = load_results(args.results)
    if args.cmd == "exit-table":
        need(args.base, "--base")
        return run_exit_table(args, recs)
    try:
        doc = load_json(args.baseline)
    except FileNotFoundError:
        if args.cmd != "baseline":
            raise
        doc = empty_baseline()
    if args.cmd == "baseline":
        need(args.reason, "--reason")
        clusters = []
        for c in args.cluster:
            g, _, name = c.partition("=")
            clusters.append((g, name))
        added, changed = write_base(doc, recs, args.reason, args.since, args.keys or ["*"], args.hashes, clusters)
        with open(args.baseline, "w") as f:
            json.dump(doc, f, indent=1, sort_keys=True)
            f.write("\n")
        bad = validate_baseline(doc)
        print("baseline %s: %d entries added, %d values changed" % (args.baseline, added, changed))
        for b in bad:
            print("FAIL " + b)
        return 1 if bad else 0

    bad = validate_baseline(doc)
    if bad:
        for b in bad:
            print("FAIL " + b)
        return 1
    rpt = check(doc, recs, skipped, args.step, args.cumulative, args.require_all)
    for ln in rpt.lines():
        print(ln)
    if args.ratchet is not None:
        if not rpt.ok:
            print("not ratcheting: the check failed")
            return 1
        if not args.ratchet.strip():
            print("legion_check: --ratchet needs a reason", file=sys.stderr)
            return 2
        when = args.date or datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        n, c = apply_ratchet(doc, rpt, args.ratchet, when)
        with open(args.baseline, "w") as f:
            json.dump(doc, f, indent=1, sort_keys=True)
            f.write("\n")
        print("ratcheted %d references, cleared %d exceptions; history line written" % (n, c))
    return 0 if rpt.ok else 1


if __name__ == "__main__":
    sys.exit(main())
