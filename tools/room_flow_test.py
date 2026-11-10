#!/usr/bin/env python3
"""The game room end to end, driven through its own buttons by TAK_ROOM_SCRIPT.

Each case runs the real debug client (a single-player one starts its own takserver, which
logs to the same stream) on a scratch profile with its own settings file, gives it a
TAK_ROOM_SCRIPT -- room actions, each through the member its button calls, each ended by its
own test within its limit -- and reads the log: the client prints "room script: <step>:
done" per step and "room script: finished", and the server its room lines. A script that
cannot finish prints why and ends the client. TAK_QUIT_TICK ends a game that started.

  1  single player (CLI game room): an AI seated, CHANGE MAP to Inner Circle, START; the
     game plays to the quit tick.
  3  CHANGE MAP to a 6-player random map, four AIs seated, then a 2-player one: the server
     removes the three AIs past its seats, and the game on it starts.

Debug builds only (TAK_ROOM_SCRIPT, TAK_QUIT_TICK and the CLI game are debug hooks).

usage: room_flow_test.py --client <takclient> --server <takserver> --data <retail-install>
                         --hpitool <hpitool> [--case N ...]
"""
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

QUIT_TICK = 60   # a started game plays this far, then the client quits


def recipe(players):
    """A small generated map for `players` (the server opens its seats from that count)."""
    return '~gen1~' + (struct.pack('<HQBHH', 10, 1234, 0, 128, 128) +
                       bytes([players, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0])).hex()


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


def played_to_quit(log, code):
    """The started game played to the quit tick and the client ended cleanly."""
    assert code == 0, 'the client exited %d' % code
    played = re.search(r'replay: wrote .*\((\d+) ticks\)', log)
    assert played and int(played[1]) >= QUIT_TICK, 'the game did not play to the quit tick'


def case_single_player(args, root):
    """CLI game room: seat an AI, CHANGE MAP, START, play to the quit tick."""
    home, env = profile(root, 'case1', 'ai:1;map:Inner Circle;start')
    code, log = run_client(args.client, ['game', 'Athri Cay', '--data', args.data], env, home)
    try:
        assert re.search(r"client \d+ created game \d+ ", log), 'no room was created'
        script_ran(log, ['ai:1', 'map:Inner Circle', 'start'])
        # The panel's PICK: the server changed the map (an AI seated on a 4-start map stays).
        assert re.search(r"game \d+: host changed the map to 'inner circle' \(capacity \d, scenario 0, 0 AI removed",
                         log, re.I), 'the server did not change the map'
        assert re.search(r'game \d+ starting with 2 players', log), 'the game did not start with the AI'
        played_to_quit(log, code)
    except AssertionError as e:
        raise AssertionError('%s\n%s' % (e, log[-4000:]))


def case_capacity(args, root):
    """CHANGE MAP to fewer seats: the AIs past them are removed."""
    six, two = recipe(6), recipe(2)
    steps = ['map:' + six, 'ai:1', 'ai:2', 'ai:3', 'ai:4', 'map:' + two, 'start']
    home, env = profile(root, 'case3', ';'.join(steps))
    code, log = run_client(args.client, ['game', 'Athri Cay', '--data', args.data], env, home)
    try:
        script_ran(log, steps)
        assert re.search(r"game \d+: host changed the map to '%s' \(capacity 6, scenario 0, 0 AI removed" % six, log), \
            'the 6-player map was not taken'
        # Capacity 2: the host keeps seat 1 and the AI in seat 2; the AIs in seats 3-5 go.
        assert re.search(r"game \d+: host changed the map to '%s' \(capacity 2, scenario 0, 3 AI removed, 0 moved" % two,
                         log), 'the server did not remove the three AIs past the 2-player map'
        assert re.search(r'game \d+ starting with 2 players', log), 'the game did not start with the AI that stayed'
        played_to_quit(log, code)
    except AssertionError as e:
        raise AssertionError('%s\n%s' % (e, log[-4000:]))


CASES = {1: case_single_player, 3: case_capacity}


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
