#!/usr/bin/env python3
"""takserver --sim-threads <auto|N>: how many CPU threads the running games' referee simulations share.

The command line, on the binary alone (no game data): `auto` and a count from 1 are accepted
(the next refusal is then about something else), 0, a negative number and junk are refused with
the option's own usage error, a missing value with the generic one, and --help lists the option.

With --data, on real servers: the total S is every CPU thread the server may use by default
(`auto`), or the count asked for (one above the usable threads means all of them). Each running
game gets S divided by the running games, rounded down, but at least 2 (1 when S is 1) and at
most 64, and the server logs that share when a game starts and whenever it changes as games
start and end. The option is scheduling only: with --seed, the game it starts has the same seed
either way.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import contextlib
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time
import server_net as auth
from server_public_test import server, login, options

USAGE = b'--sim-threads must be auto or a number of threads, 1 or more'
MAX_THREADS = 64   # sim::SimPool::kMaxThreads


def share(total, running):
    """SimPool::serverShare: each running game's threads out of the server's total."""
    return min(max(total // max(running, 1), min(2, total)), MAX_THREADS)


def usable_cpus():
    """SimPool::usableCpus(): the affinity mask on Linux, the machine's threads elsewhere."""
    try:
        return max(1, len(os.sched_getaffinity(0)))
    except AttributeError:
        return max(1, os.cpu_count() or 1)


def cli(binary, root, *arguments):
    return subprocess.run([str(binary), '--local', *arguments], cwd=root, capture_output=True, timeout=20)


def cli_checks(binary, root):
    for value in ('auto', '1', '2', '8', '64', '65', '1000', '99999999999999999999'):
        r = cli(binary, root, '--sim-threads', value)
        # Accepted: the refusal is the next check's (no --data), not this option's.
        assert r.returncode == 1 and USAGE not in r.stderr and b'--data <retail-install-dir> is required' in r.stderr, (value, r)
    for value in ('0', '-1', '-8', 'junk', 'Auto', '', '4x', '+4', '2.5', '0x10', ' 4'):
        r = cli(binary, root, '--sim-threads', value)
        assert r.returncode == 1 and USAGE in r.stderr and b'--data' not in r.stderr and not r.stdout, (value, r)
    r = cli(binary, root, '--sim-threads')   # no value
    assert r.returncode == 1 and b'unknown option or missing value: --sim-threads' in r.stderr, r
    r = cli(binary, root, '--help')
    assert r.returncode == 0 and b'[--sim-threads <auto|N>]' in r.stdout and b'--sim-threads is how many CPU threads' in r.stdout, r


def start_game(port, h, name):
    """Create a two-seat game (the host and one AI) on a small generated map and start it.
    Returns the open peer (the game runs while it stays connected) and the game's seed."""
    p = login(port, h, name)
    recipe = '~gen1~' + (struct.pack('<HQBHH', 10, 1234, 0, 128, 128) + bytes([2, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0])).hex()
    p.send('CreateGame', auth.field(name) + auth.field('') + auth.field(recipe) + auth.field('') + options() + bytes([2, 0, 0]))
    p.receive('JoinResult')
    values = auth.lobby(p.receive('LobbyState'))[3][0][0]
    auth.slot_update(p, 0, 1, values[1], values[2], values[3], 1, values[5])
    auth.slot_update(p, 1, 2, 1, 1, 1, 1, 0)
    p.send('StartGame')
    d = p.receive('GameStarting').data
    return p, struct.unpack_from('<I', d, len(d) - 16)[0]


def sim_lines(root, count):
    """The server's `sim threads:` lines, once it has logged `count` of them."""
    deadline = time.monotonic() + 20
    while True:
        lines = re.findall(r'^sim threads: .*$', (root / 'log').read_text(), re.M)
        if len(lines) >= count or time.monotonic() > deadline:
            return lines
        time.sleep(.05)


def one_game(binary, data, *arguments):
    with tempfile.TemporaryDirectory(prefix='tak-simthreads-') as tmp:
        root = Path(tmp)
        with server(binary, data, root, '--seed', '7', *arguments) as (port, h, process):
            peer, seed = start_game(port, h, 'Solo')
            with contextlib.closing(peer):
                return sim_lines(root, 1), seed


def several_games(binary, data, total):
    """Three games start one after another on a server of `total` threads, then two of them end:
    the log has the share at each start, and again once it changes."""
    with tempfile.TemporaryDirectory(prefix='tak-simthreads-') as tmp:
        root = Path(tmp)
        with server(binary, data, root, '--sim-threads', str(total)) as (port, h, process):
            first, _ = start_game(port, h, 'First')
            with contextlib.closing(first):
                second, _ = start_game(port, h, 'Second')
                third, _ = start_game(port, h, 'Third')
                started = sim_lines(root, 3)
                third.close()
                second.close()
                # Back to one game. From three games to two the share is the same for any
                # total up to 4, so that step logs nothing.
                return started, sim_lines(root, 4)[3:]


def server_checks(binary, data):
    cpus = usable_cpus()
    asked = (1, 2, MAX_THREADS, 1000)
    # One server per case, side by side (each has its own folder and port).
    with ThreadPoolExecutor(max_workers=7) as pool:
        default = pool.submit(one_game, binary, data)
        explicit = pool.submit(one_game, binary, data, '--sim-threads', 'auto')
        counts = [pool.submit(one_game, binary, data, '--sim-threads', str(n)) for n in asked]
        several = pool.submit(several_games, binary, data, 4)
        auto, auto_seed = default.result()
        assert len(auto) == 1 and re.fullmatch(r'sim threads: \d+ \(auto: .*\)', auto[0]), auto
        # Every usable CPU thread, unless a debug server inherited TAK_SIM_WORKERS (which overrides auto, and only auto).
        if 'TAK_SIM_WORKERS' not in os.environ:
            assert auto == ['sim threads: %d (auto: %d over 1 game)' % (share(cpus, 1), cpus)], (auto, cpus)
        assert explicit.result() == (auto, auto_seed)
        for n, game in zip(asked, counts):
            lines, seed = game.result()
            total = min(n, cpus)
            assert lines == ['sim threads: %d (%d over 1 game)' % (share(total, 1), total)], (n, lines)
            assert seed == auto_seed == 7, (n, seed, auto_seed)   # scheduling only
        # Several games share the total, rounded down, never fewer than 2 each (1 on a
        # one-thread server): 4 threads are 4, then 2 and 2, then 2, 2 and 2 -- more than
        # the total -- and 4 again for the game that is left.
        total = min(4, cpus)
        started, after = several.result()
        assert started == ['sim threads: %d (%d over 1 game)' % (share(total, 1), total),
                           'sim threads: %d (%d over 2 games)' % (share(total, 2), total),
                           'sim threads: %d (%d over 3 games)' % (share(total, 3), total)], started
        back = ['sim threads: %d (%d over 1 game)' % (share(total, 1), total)] if share(total, 1) != share(total, 3) else []
        assert after == back, (started, after)
        assert cpus < 4 or (share(total, 2), share(total, 3)) == (2, 2), total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--server', type=Path, required=True)
    ap.add_argument('--data', type=Path)
    a = ap.parse_args()
    binary = a.server.resolve()
    with tempfile.TemporaryDirectory(prefix='tak-simthreads-cli-') as tmp:
        cli_checks(binary, Path(tmp))
    if a.data:
        server_checks(binary, a.data.resolve())
    print('PASS takserver --sim-threads: auto and a count accepted, junk refused with the usage error'
          + ('; each game logs its share of the total, never fewer than 2 with several games' if a.data else ' (no --data: command line only)'))


if __name__ == '__main__':
    main()
