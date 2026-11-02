#!/usr/bin/env python3
"""Settings -> Benchmark's PATHFINDING choice, from the screen to the running worlds.

The screen: TAK_BENCH_MENU_TEST clicks the row where it is drawn and reads the settings
file back. The launch: TAK_MENU_PICK=benchmark starts the same menu launch a RELEASE
build makes, with the pick only in the settings file. A Legion game is told from a
Retail one by the sims themselves: only a world running Legion prints LPROBE lines
(TAK_LPROBE), and the client and its local server (the referee) both must. TAK_LEGION
still forces Legion. The run's stats panel and results must name the mode the sims ran,
and DONE must fit the window: TAK_BENCH_STOP_TICK ends each run with a full run's results,
and TAK_BENCH_REPORT prints what those two screens drew. The profile is scratch and the
settings file is named outright: SDL_GetPrefPath ignores XDG_* on macOS and Windows.

usage: benchmark_pathfinding_test.py <takclient> <retail-install>
"""
import os, re, subprocess, sys, tempfile

client, data = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])


def fail(why, log):
    sys.stdout.write(log)
    print("FAIL: " + why)
    sys.exit(1)


def run(root, name, saved, extra, args):
    """One client process on its own profile; returns its whole log."""
    home = os.path.join(root, name)
    os.makedirs(home)
    settings = os.path.join(home, "settings.ini")
    with open(settings, "w") as f:
        f.write(saved)
    env = dict(os.environ, XDG_CONFIG_HOME=home, XDG_DATA_HOME=home, SDL_VIDEODRIVER="dummy",
               SDL_AUDIODRIVER="dummy", TAK_SETTINGS_FILE=settings, **extra)
    if "TAK_LEGION" not in extra:
        env.pop("TAK_LEGION", None)
    shot = os.path.join(home, "shot.png")
    done = subprocess.run([client, "menu", "--data", data, "--shot", shot] + args, cwd=home, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
    if done.returncode != 0:
        fail("%s: exit code %d" % (name, done.returncode), done.stdout)
    return done.stdout


def named(log, name, mode, size):
    """The screens of a finished run name `mode`, and all of DONE is inside a `size` window."""
    if "BENCHSTATS PATHFINDER %s\n" % mode not in log:
        fail("%s: the stats panel must name %s" % (name, mode), log)
    if "BENCHRESULT PATHFINDING %s\n" % mode not in log:
        fail("%s: the results must name %s" % (name, mode), log)
    done = re.search(r"^BENCHDONE (\d+) (\d+) (\S+) (\S+) (\S+) (\S+)$", log, re.M)
    if not done:
        fail("%s: the results never drew DONE" % name, log)
    w, h, x, y, dw, dh = (float(v) for v in done.groups())
    if (w, h) != size or x < 0 or y < 0 or x + dw > w or y + dh > h:
        fail("%s: DONE at %s must be inside a %s window" % (name, done[0], size), log)


def probes(root, name, saved, size, extra=None):
    """A menu Benchmark in a `size` window, up to its results; {tick: LPROBE lines printed} over both sims."""
    log = run(root, name, saved + "fullscreen = 0\n", dict({"TAK_MENU_PICK": "benchmark", "TAK_LPROBE": "30", "TAK_SPEED": "80",
                                                          "TAK_QUIT_TICK": "100000", "TAK_SHOT_MS": "100000", "TAK_BENCH_STOP_TICK": "45",
                                                          "TAK_BENCH_REPORT": "1"}, **(extra or {})),
              ["--winsize", str(size[0]), str(size[1])])
    ticks = {}
    for m in re.finditer(r"^LPROBE tick=(\d+) ", log, re.M):
        ticks[int(m[1])] = ticks.get(int(m[1]), 0) + 1
    return ticks, log


with tempfile.TemporaryDirectory(prefix="tak-bench-path-") as root:
    log = run(root, "screen", "", {"TAK_BENCH_MENU_TEST": "1"}, [])
    if "PASS: Benchmark pathfinding row picks and saves" not in log:
        fail("the Benchmark screen's row", log)

    ticks, log = probes(root, "legion", "benchmarkPathfinding = 4\n", (1024, 768))
    if ticks.get(30, 0) < 2:
        fail("a saved Legion pick must run Legion on the client and the referee (got %s)" % ticks, log)
    named(log, "legion", "LEGION", (1024, 768))

    ticks, log = probes(root, "retail", "benchmarkPathfinding = 0\n", (1280, 800))
    if ticks:
        fail("a saved Retail pick must run Retail (got %s)" % ticks, log)
    if "connected to 127.0.0.1" not in log:
        fail("the Retail benchmark never reached its server", log)
    named(log, "retail", "RETAIL", (1280, 800))

    ticks, log = probes(root, "flag", "benchmarkPathfinding = 0\n", (640, 480), {"TAK_LEGION": "1"})
    if ticks.get(30, 0) < 2:
        fail("TAK_LEGION must still force Legion (got %s)" % ticks, log)
    named(log, "flag", "LEGION", (640, 480))
print("PASS: the Benchmark's pathfinding pick reaches the running worlds")
