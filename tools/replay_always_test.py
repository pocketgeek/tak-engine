#!/usr/bin/env python3
"""A replay is saved however a game ends.

Real takserver + headless takclient games (Inner Circle, --seed 1), each in its own
scratch profile, ended in different ways:

  crash      the client SIGKILLs itself at a known tick (TAK_KILL_AT_TICK; on
             Windows TerminateProcess): the next start recovers a replay holding
             nearly every tick it had received;
  kill       the test kills the client from outside (SIGKILL / Task Manager style);
  result     the player is defeated and play goes on for the three-second results
             transition: those ticks are in the replay, not cut off at the result;
  sigterm    SIGTERM mid-game (POSIX): a clean teardown finishes the file itself;
  disconnect the server dies mid-game: the client finishes the replay it has;
  server-sigterm  a --replaydir server stopped with SIGTERM (POSIX) writes the
             replay of the game still running, and the client keeps its own;
  mission    a campaign mission (--mpmission) played to its time limit: recorded
             like a skirmish, its header naming the mission and the data hash the
             client also reports at Loaded;
  mission-crash  the same mission, the client SIGKILLed mid-game: recovered.

Every replay is then played back (TAK_REPLAY_VERIFY) and must match the recorded
hash checkpoints. Old-format files loading is replay_file's job; the byte layout of a
streamed file is replay_writer's.
"""
import argparse
import concurrent.futures
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

MAP = 'Inner Circle'
MISSION = 'takmission01_mt'


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def environment(root):
    env = {k: v for k, v in os.environ.items()
           if not k.startswith('TAK_') or k in ('TAK_LEGION',)}
    for name in ('data', 'config', 'home'):
        (root / name).mkdir(parents=True, exist_ok=True)
    env.update(XDG_DATA_HOME=str(root / 'data'), XDG_CONFIG_HOME=str(root / 'config'),
               TAK_HEADLESS='1', SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    if os.name != 'nt':
        env['HOME'] = str(root / 'home')
    return env


def replays(root, suffix):
    return sorted(p for p in (root / 'data').rglob('*' + suffix) if p.name.endswith(suffix))


def header_fields(path):
    """(mission stem, data hash) from a .takrep header (format 13)."""
    d = path.read_bytes()
    pos = 12

    def s():
        nonlocal pos
        n = struct.unpack_from('<H', d, pos)[0]
        v = d[pos + 2:pos + 2 + n].decode('utf-8', 'replace')
        pos += 2 + n
        return v
    s(); pos += 3 + 4 + 2 + 4 + 2       # map, flags, cap, flags, seed, starts+bench
    mission = s(); s()                  # mission, engine
    return mission, struct.unpack_from('<Q', d, pos)[0]


def complete_ticks(path):
    """Complete bundles in a (possibly still growing) .takrep / .takrep.part."""
    try:
        d = path.read_bytes()
    except OSError:
        return 0
    if len(d) < 12 or d[:4] != b'TAKR':
        return 0
    pos = 12

    def u(fmt):
        nonlocal pos
        v = struct.unpack_from(fmt, d, pos)[0]
        pos += struct.calcsize(fmt)
        return v

    def s():
        nonlocal pos
        n = u('<H')
        pos += n
    try:
        s(); pos += 3; u('<I'); pos += 2; u('<I'); pos += 2   # map, flags, cap, flags, seed, starts+bench
        s(); s(); u('<Q'); pos += 1; s(); s(); pos += 1       # mission, engine, data hash, sight, digests, path mode
        slots = u('<B'); pos += 5 * slots + 1                  # slot table, map scenario (format 13)
        pos += 4                                               # the count (open while recording)
        n = 0
        while pos + 4 <= len(d):
            size = struct.unpack_from('<I', d, pos)[0]
            if pos + 4 + size > len(d) or struct.unpack_from('<I', d, pos + 4)[0] != n:
                break
            pos += 4 + size
            n += 1
        return n
    except struct.error:
        return 0


class Game:
    def __init__(self, args, root, client_env=None, time_limit=120, server_args=(), mission=None):
        self.args, self.root = args, root
        self.env = environment(root)
        self.port = free_port()
        self.server_log = open(root / 'server.log', 'w')
        self.server = subprocess.Popen(
            [args.server, '--port', str(self.port), '--data', args.data, '--no-auth', '--local', '--seed', '1',
             *server_args],
            stdout=self.server_log, stderr=subprocess.STDOUT, env=self.env)
        deadline = time.monotonic() + 90
        while 'listening' not in (root / 'server.log').read_text(errors='replace'):
            if self.server.poll() is not None or time.monotonic() > deadline:
                raise AssertionError('server did not start: ' + (root / 'server.log').read_text(errors='replace'))
            time.sleep(0.2)
        env = dict(self.env)
        env.update(client_env or {})
        self.client_log = open(root / 'client.log', 'w')
        mode = ['--mpmission', mission] if mission else ['--mpai']
        self.client = subprocess.Popen(
            [args.client, 'game', mission or MAP, '--data', args.data, '--server', '127.0.0.1',
             '--serverport', str(self.port), *mode, '--time', str(time_limit)],
            stdout=self.client_log, stderr=subprocess.STDOUT, env=env)

    def log(self):
        return (self.root / 'client.log').read_text(errors='replace')

    def wait_recorded(self, ticks, timeout=150):
        """Wait until the recording on disk holds `ticks` complete ticks; returns them."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            parts = replays(self.root, '.takrep.part')
            if parts and complete_ticks(parts[0]) >= ticks:
                return parts[0], complete_ticks(parts[0])
            if self.client.poll() is not None:
                raise AssertionError('client exited early:\n' + self.log())
            time.sleep(0.1)
        raise AssertionError('recording never reached %d ticks:\n%s' % (ticks, self.log()))

    def wait_client(self, timeout=120):
        try:
            return self.client.wait(timeout)
        except subprocess.TimeoutExpired:
            self.client.kill()
            raise AssertionError('client did not exit:\n' + self.log())

    def close(self):
        for p in (self.client, self.server):
            if p.poll() is None:
                p.kill()
                p.wait()
        self.server_log.close()
        self.client_log.close()


def verify(args, root, path):
    """Play `path` back headless (the start recovers unfinished recordings first);
    returns its tick count. Fails on a refused or diverging replay."""
    env = environment(root)
    env['TAK_REPLAY_VERIFY'] = '1'
    out = subprocess.run([args.client, 'replay', str(path), '--data', args.data], env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace',
                         timeout=300).stdout
    m = re.search(r'replay done: tick=(\d+) hash=\w+ units=\d+ framed=\d+(.*)', out)
    assert m and 'DIVERGED' not in m.group(2), 'replay did not play back cleanly:\n' + out
    return int(m.group(1)), out


def finished_one(root):
    done, parts = replays(root, '.takrep'), replays(root, '.takrep.part')
    assert len(done) == 1 and not parts, (done, parts)
    return done[0]


def case_crash(args, root):
    game = Game(args, root, {'TAK_KILL_AT_TICK': '450'})
    try:
        game.wait_client()
        m = re.search(r'kill-at-tick: killing the client at tick (\d+) with (\d+) ticks received', game.log())
        assert m, game.log()
        received = int(m.group(2))
        parts = replays(root, '.takrep.part')
        assert len(parts) == 1 and not replays(root, '.takrep'), 'a killed client left no recording'
    finally:
        game.close()
    final = parts[0].with_name(parts[0].name[:-len('.part')])
    ticks, out = verify(args, root, final)
    assert 'replay: recovered' in out, out
    finished_one(root)
    # Flushed about once a second (30 ticks at 1x): a crash loses no more than that.
    assert received - 45 <= ticks <= received, 'recovered %d of %d ticks' % (ticks, received)
    return 'crash at tick %s: recovered %d of %d ticks received' % (m.group(1), ticks, received)


def case_kill(args, root):
    game = Game(args, root)
    try:
        _, seen = game.wait_recorded(300)
        game.client.kill()   # SIGKILL; TerminateProcess on Windows
        game.client.wait()
    finally:
        game.close()
    part = replays(root, '.takrep.part')
    assert len(part) == 1, part
    ticks, _ = verify(args, root, part[0].with_name(part[0].name[:-len('.part')]))
    finished_one(root)
    assert ticks >= seen, 'recovered %d ticks, %d were on disk before the kill' % (ticks, seen)
    return 'killed from outside: recovered %d ticks (%d seen on disk before the kill)' % (ticks, seen)


def case_result(args, root):
    game = Game(args, root, {'TAK_DEFEAT_TEST': '1'})
    try:
        assert game.wait_client() == 0, game.log()
        log = game.log()
    finally:
        game.close()
    assert 'PASS: automatic defeat results' in log, log
    at = re.search(r'replay: result at tick (\d+), (\d+) ticks recorded; recording continues', log)
    wrote = re.search(r'replay: wrote .* \((\d+) ticks\)', log)
    assert at and wrote, log
    result, total = int(at.group(1)), int(wrote.group(1))
    # The three-second transition after the result is about 90 ticks of play.
    assert total >= result + 60, 'replay stops at tick %d, the result came at %d' % (total, result)
    ticks, _ = verify(args, root, finished_one(root))
    assert ticks == total
    return 'defeated at tick %d: replay runs to tick %d' % (result, total)


def case_sigterm(args, root):
    game = Game(args, root)
    try:
        _, seen = game.wait_recorded(200)
        game.client.send_signal(signal.SIGTERM)
        game.wait_client()
        log = game.log()
    finally:
        game.close()
    path = finished_one(root)   # finished by the client itself, no recovery needed
    wrote = re.search(r'replay: wrote .* \((\d+) ticks\)', log)
    assert wrote and int(wrote.group(1)) >= seen, log
    ticks, out = verify(args, root, path)
    assert 'replay: recovered' not in out
    return 'SIGTERM: client finished its own replay of %d ticks' % ticks


def case_disconnect(args, root):
    game = Game(args, root)
    try:
        _, seen = game.wait_recorded(200)
        game.server.kill()
        game.server.wait()
        game.wait_client()
        log = game.log()
    finally:
        game.close()
    path = finished_one(root)
    wrote = re.search(r'replay: wrote .* \((\d+) ticks\)', log)
    assert wrote and int(wrote.group(1)) >= seen, log
    ticks, _ = verify(args, root, path)
    return 'server died: client finished its replay of %d ticks' % ticks


def case_server_sigterm(args, root):
    served = root / 'server-replays'
    served.mkdir(parents=True)
    game = Game(args, root, server_args=('--replaydir', str(served)))
    try:
        _, seen = game.wait_recorded(200)
        game.server.send_signal(signal.SIGTERM)
        assert game.server.wait(60) == 0, (root / 'server.log').read_text(errors='replace')
        game.wait_client()
    finally:
        game.close()
    kept = sorted(served.glob('*.takrep'))
    assert len(kept) == 1 and not list(served.glob('*.tmp')), kept
    server_ticks, _ = verify(args, root, kept[0])
    client_ticks, _ = verify(args, root, finished_one(root))
    assert server_ticks >= seen and client_ticks >= seen, (server_ticks, client_ticks, seen)
    return 'server SIGTERM: server kept %d ticks, the client %d' % (server_ticks, client_ticks)


def case_mission(args, root):
    game = Game(args, root, time_limit=20, mission=MISSION)
    try:
        assert game.wait_client() == 0, game.log()
        log = game.log()
    finally:
        game.close()
    assert 'end=timelimit' in log, log
    wrote = re.search(r'replay: wrote .* \((\d+) ticks\)', log)
    assert wrote, 'a campaign mission was not recorded:\n' + log
    path = finished_one(root)
    mission, data_hash = header_fields(path)
    # The same member is reported at Loaded: 0 there would skip the server's check.
    assert mission == MISSION and data_hash != 0, (mission, hex(data_hash))
    ticks, _ = verify(args, root, path)
    assert ticks == int(wrote.group(1)) >= 590, (ticks, wrote.group(1))
    return 'mission %s: replay of %d ticks, data hash %016x' % (mission, ticks, data_hash)


def case_mission_crash(args, root):
    game = Game(args, root, {'TAK_KILL_AT_TICK': '300'}, time_limit=60, mission=MISSION)
    try:
        game.wait_client()
        m = re.search(r'kill-at-tick: killing the client at tick (\d+) with (\d+) ticks received', game.log())
        assert m, game.log()
        received = int(m.group(2))
        parts = replays(root, '.takrep.part')
        assert len(parts) == 1 and not replays(root, '.takrep'), 'a killed mission client left no recording'
    finally:
        game.close()
    ticks, out = verify(args, root, parts[0].with_name(parts[0].name[:-len('.part')]))
    assert 'replay: recovered' in out, out
    path = finished_one(root)
    assert header_fields(path)[0] == MISSION
    assert received - 45 <= ticks <= received, 'recovered %d of %d ticks' % (ticks, received)
    return 'mission crash at tick %s: recovered %d of %d ticks received' % (m.group(1), ticks, received)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--server', required=True)
    ap.add_argument('--client', required=True)
    ap.add_argument('--data', required=True)
    ap.add_argument('--case', action='append', help='run only these cases')
    args = ap.parse_args()
    cases = {'crash': case_crash, 'kill': case_kill, 'result': case_result,
             'sigterm': case_sigterm, 'disconnect': case_disconnect, 'server-sigterm': case_server_sigterm,
             'mission': case_mission, 'mission-crash': case_mission_crash}
    if os.name == 'nt':   # Windows delivers no SIGTERM; a kill is the 'kill' case
        del cases['sigterm'], cases['server-sigterm']
    names = args.case or list(cases)
    base = Path(tempfile.mkdtemp(prefix='tak-replay-always-'))
    failed = False
    try:
        with concurrent.futures.ThreadPoolExecutor(len(names)) as pool:
            jobs = {name: pool.submit(cases[name], args, base / name) for name in names}
            for name, job in jobs.items():
                try:
                    print('PASS %s: %s' % (name, job.result()), flush=True)
                except Exception as e:  # report every case, then fail
                    failed = True
                    print('FAIL %s: %s' % (name, e), flush=True)
    finally:
        if not failed:
            shutil.rmtree(base, ignore_errors=True)
        else:
            print('logs kept in', base)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
