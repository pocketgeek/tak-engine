#!/usr/bin/env python3
"""takserver --sim-threads <auto|N>: how many CPU threads one game's referee simulation may use.

The command line, on the binary alone (no game data): `auto` and a count from 1 are accepted
(the next refusal is then about something else), 0, a negative number and junk are refused with
the option's own usage error, a missing value with the generic one, and --help lists the option.

With --data, on real servers: each game start logs what its simulation runs on -- the count
asked for, `(auto)` for the CPU rule, a count above the machine's usable threads as all of them
(at most 64) -- and while more than one game runs each ticks on one thread, whatever was asked.
The option is scheduling only: with --seed, the game it starts has the same seed either way.
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


def two_games(binary, data):
    """More than one running game: each ticks on one thread, and the log says so."""
    with tempfile.TemporaryDirectory(prefix='tak-simthreads-') as tmp:
        root = Path(tmp)
        with server(binary, data, root, '--sim-threads', '2') as (port, h, process):
            first, _ = start_game(port, h, 'First')
            with contextlib.closing(first):
                second, _ = start_game(port, h, 'Second')
                with contextlib.closing(second):
                    return sim_lines(root, 2)


def server_checks(binary, data):
    cpus = min(usable_cpus(), MAX_THREADS)
    asked = (1, 2, MAX_THREADS, 1000)
    # One server per case, side by side (each has its own folder and port).
    with ThreadPoolExecutor(max_workers=7) as pool:
        default = pool.submit(one_game, binary, data)
        explicit = pool.submit(one_game, binary, data, '--sim-threads', 'auto')
        counts = [pool.submit(one_game, binary, data, '--sim-threads', str(n)) for n in asked]
        several = pool.submit(two_games, binary, data)
        auto, auto_seed = default.result()
        assert len(auto) == 1 and re.fullmatch(r'sim threads: \d+ \(auto\)', auto[0]), auto
        # The CPU rule, unless a debug server inherited TAK_SIM_WORKERS (which overrides AUTO, and only AUTO).
        if 'TAK_SIM_WORKERS' not in os.environ:
            assert auto == ['sim threads: %d (auto)' % (1 + min(3, usable_cpus() // 8))], (auto, usable_cpus())
        assert explicit.result() == (auto, auto_seed)
        for n, game in zip(asked, counts):
            lines, seed = game.result()
            assert lines == ['sim threads: %d' % min(n, cpus)], (n, lines)
            assert seed == auto_seed == 7, (n, seed, auto_seed)   # scheduling only
        lines = several.result()
        assert lines == ['sim threads: %d' % min(2, cpus), 'sim threads: 1 (2 games running)'], lines


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
          + ('; each game logs its sim threads, several games tick serially' if a.data else ' (no --data: command line only)'))


if __name__ == '__main__':
    main()
