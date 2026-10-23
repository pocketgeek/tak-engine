#!/usr/bin/env python3
"""A game mod's n_ai.txt reaches the server's Normal AI through the real mod transfer.

Two headless --mpai games on the same seed, the AI seat on Normal. The control game
runs a game pack whose n_ai.txt only carries a comment, and the AI builds the
watched units; the modded game's pack says `limit <unit> 0` for them, and no AI
ever builds one. The pack is ticked in the host's settings, uploaded with the room's
game mods and applied by the server (the only place the AI runs).
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from server_public_test import server

# Units the Normal AI (Taros, the room's seat 1) builds within two minutes on Inner
# Circle seed 1 -- the control game checks it still does.
WATCHED = ('tarcage', 'targarg')
SECONDS = '120'


def install(root, data, name, n_ai):
    target = root / name
    target.mkdir()
    for f in data.resolve().iterdir():
        if f.name.lower() in ('overrides', 'mods', 'modcache', 'overridecache', 'mapcache'):
            continue
        (target / f.name).symlink_to(f, target_is_directory=f.is_dir())
    pack = target / 'mods' / 'game' / 'my_ai'
    pack.mkdir(parents=True)
    (pack / 'n_ai.txt').write_text(n_ai)
    return target


def play(a, root, name, n_ai):
    data = install(root, a.data, name, n_ai)
    work = root / (name + '-run')
    work.mkdir()
    roster = work / 'roster.jsonl'
    os.environ['TAK_ROSTER_DUMP'] = str(roster)   # the server reads it at its first sample
    os.environ['TAK_ROSTER_EVERY'] = '150'
    try:
        with server(a.server.resolve(), data, work, '--no-auth', '--seed', '1') as (port, _, process):
            settings = work / 'settings.ini'
            settings.write_text('mods.gamePack = ' + 'my_ai'.encode().hex() + '\n')
            env = dict(os.environ, HOME=str(work), XDG_DATA_HOME=str(work), XDG_CONFIG_HOME=str(work),
                       TAK_SETTINGS_FILE=str(settings), TAK_HEADLESS='1', TAK_AI_LEVEL='2', TAK_SPEED='80',
                       SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
            env.pop('TAK_ROSTER_DUMP')
            result = subprocess.run([str(a.client.resolve()), 'game', 'Inner Circle', '--data', str(data),
                                     '--server', '127.0.0.1', '--serverport', str(port), '--mpai', '--time', SECONDS],
                                    env=env, capture_output=True, text=True, timeout=600)
            log = (work / 'log').read_text()
            assert result.returncode == 0 and 'hash=' in result.stdout + result.stderr, \
                result.stdout + result.stderr + '\n' + log
            assert process.poll() is None, log
    finally:
        os.environ.pop('TAK_ROSTER_DUMP', None)
        os.environ.pop('TAK_ROSTER_EVERY', None)
    built, levels = set(), {}
    for line in roster.read_text().splitlines():
        row = json.loads(line)
        if row['kind'] == 'meta':
            levels = {p['p']: p['level'] for p in row['players']}
        else:
            built.update(u[2] for u in row['units'] if levels.get(u[1]) == 2)
    ai = [v for v in levels.values() if v >= 0]
    assert ai and all(v == 2 for v in ai), levels
    return built, log


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--server', type=Path, required=True)
    p.add_argument('--client', type=Path, required=True)
    p.add_argument('--data', type=Path, required=True)
    a = p.parse_args()
    with tempfile.TemporaryDirectory(prefix='tak-ai-mod-') as tmp:
        root = Path(tmp)
        control, log = play(a, root, 'control', '// a Normal profile that changes nothing\n')
        assert 'n_ai.txt' in log, log
        missing = [u for u in WATCHED if u not in control]
        assert not missing, 'the control game never built %s (built: %s)' % (missing, sorted(control))
        modded, log = play(a, root, 'modded', ''.join('limit %s 0\n' % u for u in WATCHED))
        assert 'AI level 2 plays the built-in profile adjusted by the game mods\' n_ai.txt' in log, log
        assert log.count(' plays the built-in profile adjusted') == 1, log   # Normal only
        banned = [u for u in WATCHED if u in modded]
        assert not banned, 'the Normal AI built %s despite the mod\'s n_ai.txt' % banned
        print('PASS: n_ai.txt travels with the game mods; the control game built %s, the modded one none'
              % ', '.join(WATCHED))


if __name__ == '__main__':
    main()
