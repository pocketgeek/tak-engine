#!/usr/bin/env python3
"""Command-line polish: the dev tools never end silently on a bad command line, the
Cartographer answers --version/--help without opening the editor, and simperf's default
generated map runs (its recipe version is the current one).

Usage text goes to stderr with exit 2 (a tool given too few arguments), --version and
--help exit 0 with their text on stdout.
"""
import argparse
import os
import subprocess
import sys
import tempfile

failures = []


def run(cmd, **kw):
    env = dict(os.environ)
    home = tempfile.mkdtemp(prefix="tak-cli-")   # never touch the real profile
    env.update(HOME=home, XDG_DATA_HOME=home, XDG_CONFIG_HOME=home, SDL_VIDEODRIVER="dummy")
    return subprocess.run(cmd, capture_output=True, text=True, timeout=120, env=env, **kw)


def check(name, ok, detail=""):
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + detail))
    if not ok:
        failures.append(name)


def usage(tool, label, *args):
    r = run([tool, *args])
    check(label, r.returncode == 2 and "usage:" in r.stderr and not r.stdout,
          f"rc={r.returncode} stdout={r.stdout!r} stderr={r.stderr!r}")


def main():
    ap = argparse.ArgumentParser()
    for n in ("cobtool", "modeltool", "gaftool", "tnttool", "tdftool", "hpitool", "simperf", "cartographer"):
        ap.add_argument("--" + n)
    ap.add_argument("--data")
    a = ap.parse_args()

    # Incomplete command lines print usage (stderr, exit 2) -- they used to exit silently.
    if a.cobtool:
        usage(a.cobtool, "cobtool disasm without a script", "disasm", "x.cob")
        usage(a.cobtool, "cobtool unknown command", "bogus", "x.cob")
        usage(a.cobtool, "cobtool no arguments")
    if a.modeltool:
        usage(a.modeltool, "modeltool obj without output", "obj", "x.3do")
        usage(a.modeltool, "modeltool wire without output", "wire", "x.3do")
        usage(a.modeltool, "modeltool unknown command", "bogus", "x.3do")
    if a.gaftool:
        usage(a.gaftool, "gaftool export without outdir", "export", "x.gaf", "x.pal")
        usage(a.gaftool, "gaftool unknown command", "bogus", "x.gaf", "x.pal")
    if a.tnttool:
        usage(a.tnttool, "tnttool heightmap without output", "heightmap", "x.tnt")
        usage(a.tnttool, "tnttool render without output", "render", "x.tnt", "dir")
        usage(a.tnttool, "tnttool unknown command", "bogus", "x.tnt")
    if a.tdftool:
        usage(a.tdftool, "tdftool get without key", "get", "x.tdf", "section")
        usage(a.tdftool, "tdftool unknown command", "bogus", "x.tdf")
    if a.hpitool:
        usage(a.hpitool, "hpitool extract without outdir", "extract", "x.hpi")
        usage(a.hpitool, "hpitool unknown command", "bogus", "x.hpi")

    if a.cartographer:
        # Neither flag may reach the editor: no window, no data-folder prompt, no profile.
        r = run([a.cartographer, "--version"])
        check("cartographer --version", r.returncode == 0 and r.stdout.startswith("cartographer (TAK engine) ")
              and " (build " in r.stdout, f"rc={r.returncode} out={r.stdout!r} err={r.stderr!r}")
        r2 = run([a.cartographer, "-v"])
        check("cartographer -v matches --version", r2.returncode == 0 and r2.stdout == r.stdout)
        r = run([a.cartographer, "--help"])
        check("cartographer --help lists --version", r.returncode == 0 and "--version" in r.stdout
              and "usage: cartographer" in r.stdout, f"rc={r.returncode} out={r.stdout!r}")

    if a.simperf and a.data:
        r = run([a.simperf, "--mode", "match", "--units", "8", "--ticks", "5", "--data", a.data])
        check("simperf default generated map runs",
              r.returncode == 0 and "recipe" not in r.stderr and "map=" in r.stdout,
              f"rc={r.returncode} out={r.stdout[-300:]!r} err={r.stderr[-300:]!r}")

    if failures:
        print(f"{len(failures)} failure(s)")
        return 1
    return 0


sys.exit(main())
