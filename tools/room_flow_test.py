#!/usr/bin/env python3
"""The game room end to end, driven through its own buttons by TAK_ROOM_SCRIPT.

Each case runs the real debug client (a single-player one starts its own takserver, which
logs to the same stream) on a scratch profile with its own settings file, gives it a
TAK_ROOM_SCRIPT -- room actions, each through the member its button calls, each ended by its
own test within its limit -- and reads the log: the client prints "room script: <step>:
done" per step and "room script: finished", and the server its room lines. A script that
cannot finish prints why and ends the client. TAK_QUIT_TICK ends a game that started.

  1  single player (CLI game room): an AI seated, START; the game plays to the quit tick.
  2  single player WATCH: the host leaves its seat, AIs fill the room, START; the host plays
     the game as a spectator.
  4  multiplayer unready: a guest READY, the host changes FOG OF WAR, the server clears the
     guest's READY (the guest is told so), the guest readies again and the host STARTs.

Debug builds only (TAK_ROOM_SCRIPT, TAK_QUIT_TICK and the CLI game are debug hooks).

usage: room_flow_test.py --client <takclient> --server <takserver> --data <retail-install>
                         --hpitool <hpitool> [--case N ...]
"""
import argparse
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time

QUIT_TICK = 60   # a started game plays this far, then the client quits


def profile(root, name, script, extra=None):
    """A scratch profile (data, config, settings file) and the client's environment."""
    home = Path(root) / name
    for sub in ('data', 'config', 'home'):
        (home / sub).mkdir(parents=True, exist_ok=True)
    settings = home / 'settings.ini'
    settings.write_text('fullscreen = 0\n')
    env = {k: v for k, v in os.environ.items() if not k.startswith('TAK_')}
    env.update(XDG_DATA_HOME=str(home / 'data'), XDG_CONFIG_HOME=str(home / 'config'), HOME=str(home / 'home'),
               TAK_SETTINGS_FILE=str(settings), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               TAK_ROOM_SCRIPT=script, TAK_QUIT_TICK=str(QUIT_TICK), TAK_SPEED='40')
    env.update(extra or {})
    return home, env


def run_client(client, argv, env, home, timeout=180):
    """One client run to its end; its whole log (stdout and stderr, its server's lines too)."""
    try:
        done = subprocess.run([client] + argv + ['--winsize', '960', '540'], cwd=home, env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace',
                              timeout=timeout)
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode(errors='replace') if isinstance(e.stdout, bytes) else (e.stdout or '')
        raise AssertionError('the client was still running after %d s:\n%s' % (timeout, out[-4000:]))
    return done.returncode, done.stdout


def script_ran(log, steps):
    """Every step finished, in order, and nothing failed."""
    for line in log.splitlines():
        if line.startswith('room script: ') and not re.search(r': done$|^room script: (finished|wrote )', line):
            raise AssertionError('the room script failed: %s' % line)
    at = 0
    for step in steps:
        found = log.find('room script: %s: done' % step, at)
        assert found >= 0, 'step %r never finished' % step
        at = found
    assert 'room script: finished' in log, 'the script did not finish'


def case_single_player(args, root):
    """CLI game room: seat an AI, START, play to the quit tick."""
    home, env = profile(root, 'case1', 'ai:1;start')
    code, log = run_client(args.client, ['game', 'Athri Cay', '--data', args.data], env, home)
    try:
        assert re.search(r"client \d+ created game \d+ ", log), 'no room was created'
        script_ran(log, ['ai:1', 'start'])
        assert re.search(r'game \d+ starting with 2 players', log), 'the game did not start with the AI'
        assert code == 0, 'the client exited %d' % code
        played = re.search(r'replay: wrote .*\((\d+) ticks\)', log)
        assert played and int(played[1]) >= QUIT_TICK, 'the game did not play to the quit tick'
    except AssertionError as e:
        raise AssertionError('%s\n%s' % (e, log[-4000:]))


def case_watch(args, root):
    """Single player WATCH: the host watches, AIs fill the room, START as a spectator."""
    home, env = profile(root, 'case2', 'watch;start')
    code, log = run_client(args.client, ['game', 'Athri Cay', '--data', args.data], env, home)
    try:
        script_ran(log, ['watch', 'start'])
        assert re.search(r'client \d+ now watches game \d+', log), 'the server never had the host watching'
        started = re.search(r'game \d+ starting with (\d+) players', log)
        assert started and int(started[1]) >= 2, 'the AIs did not fill the room'
        assert code == 0, 'the client exited %d' % code
        played = re.search(r'replay: wrote .*\((\d+) ticks\)', log)
        assert played and int(played[1]) >= QUIT_TICK, 'the watched game did not play to the quit tick'
    except AssertionError as e:
        raise AssertionError('%s\n%s' % (e, log[-4000:]))


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def case_unready(args, root):
    """Multiplayer: the host's rule change clears a READY guest; it readies again; START."""
    port = free_port()
    shome, senv = profile(root, 'case4-server', '')
    slog_path = shome / 'server.log'
    slog = open(slog_path, 'w')
    server = subprocess.Popen([args.server, '--port', str(port), '--data', args.data, '--local', '--seed', '1', '--no-auth'],
                              stdout=slog, stderr=subprocess.STDOUT, env=senv, cwd=shome)
    clients = []
    try:
        deadline = time.monotonic() + 90
        while 'listening' not in slog_path.read_text(errors='replace'):
            assert time.monotonic() < deadline and server.poll() is None, \
                'the server never listened:\n' + slog_path.read_text(errors='replace')[-3000:]
            time.sleep(0.1)
        net = ['game', 'Athri Cay', '--data', args.data, '--server', '127.0.0.1', '--serverport', str(port)]
        # Windowed clients (the room draws, and tells), driven by the harness modes: the host
        # (1) waits for nine READY (never) instead of starting itself, so its script STARTs;
        # the guest (2) joins, readies, and readies again when its READY is cleared.
        hhome, henv = profile(root, 'case4-host', 'wait:ready=2;opt:fog=2;wait:ready=2;start',
                              {'TAK_MPAUTO': '1', 'TAK_MP_WAIT': '9'})
        ghome, genv = profile(root, 'case4-guest', '', {'TAK_MPAUTO': '2'})
        for name, home, env in (('host', hhome, henv), ('guest', ghome, genv)):
            log = open(home / 'client.log', 'w')
            clients.append((name, home, subprocess.Popen([args.client] + net + ['--winsize', '960', '540'], cwd=home,
                                                         env=env, stdout=log, stderr=subprocess.STDOUT), log))
        logs = {}
        for name, home, proc, log in clients:
            try:
                code = proc.wait(180)
            except subprocess.TimeoutExpired:
                proc.kill(); proc.wait()
                code = 'still running after 180 s'
            log.close()
            logs[name] = (code, (home / 'client.log').read_text(errors='replace'))
        server.kill(); server.wait(); slog.close()
        slog_text = slog_path.read_text(errors='replace')
        try:
            script_ran(logs['host'][1], ['wait:ready=2', 'opt:fog=2', 'wait:ready=2', 'start'])
            assert re.search(r'game \d+: host changed the rules \(fog[^;]*; rev \d+, 1 unreadied\)', slog_text), \
                'the fog change did not unready the guest'
            assert 'room note: HOST CHANGED FOG OF WAR -- READY AGAIN' in logs['guest'][1], 'the guest was not told'
            assert 'room note: PLAYERS MUST READY AGAIN' in logs['host'][1], 'the host was not told'
            assert re.search(r'game \d+ starting with 2 players', slog_text), 'the game did not start'
            for name, (code, log) in logs.items():
                assert code == 0, 'the %s exited %s' % (name, code)
                played = re.search(r'replay: wrote .*\((\d+) ticks\)', log)
                assert played and int(played[1]) >= QUIT_TICK, 'the %s did not play to the quit tick' % name
        except AssertionError as e:
            raise AssertionError('%s\nhost:\n%s\nguest:\n%s\nserver:\n%s' % (
                e, logs['host'][1][-2500:], logs['guest'][1][-2500:], slog_text[-2500:]))
    finally:
        for _, _, proc, log in clients:
            if proc.poll() is None:
                proc.kill(); proc.wait()
            log.close()
        if server.poll() is None:
            server.kill(); server.wait()
        slog.close()


CASES = {1: case_single_player, 2: case_watch, 4: case_unready}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--client', required=True)
    ap.add_argument('--server', required=True)    # the multiplayer cases' own server
    ap.add_argument('--data', required=True)
    ap.add_argument('--hpitool', required=True)   # packs the --play-map case's snapshot
    ap.add_argument('--case', type=int, action='append', choices=sorted(CASES))
    args = ap.parse_args()
    for name in ('client', 'server', 'data', 'hpitool'):
        setattr(args, name, os.path.abspath(getattr(args, name)))
    failed = 0
    with tempfile.TemporaryDirectory(prefix='tak-room-flow-') as root:
        for n in args.case or sorted(CASES):
            try:
                CASES[n](args, root)
                print('PASS: case %d (%s)' % (n, CASES[n].__doc__.split('\n')[0]))
            except AssertionError as e:
                print('FAIL: case %d: %s' % (n, e))
                failed += 1
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
