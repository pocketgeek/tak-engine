#!/usr/bin/env python3
"""A game mod's ai/n_ai.txt reaches the server's Normal AI through the real mod transfer.

Headless all-AI games on AI Circle (Inner Circle's map in a single-map archive under
Maps/), seed 1, a spectating host: seat 0 Aramon on Easy, seat 1 Taros on Normal.
Both games tick a game pack, uploaded with the room's game mods and applied by the
server (the only place the AI runs).

  control  the pack's ai/n_ai.txt only carries a comment. Every misplaced n_ai.txt
           says `limit <unit> 0` for the Normal seat's watched units -- at the pack's
           root, in a cosmetic pack's ai/, in a base install archive's ai/ and in the
           map archive's ai/ -- and none is read: the Normal AI builds them
  modded   the pack's ai/n_ai.txt says `limit <unit> 0` for both seats' watched
           units: the Normal AI never builds its own, the Easy AI still builds its
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from server_public_test import server

# Units each AI builds within two minutes in this game (seat -> units).
EASY_SEAT, NORMAL_SEAT = 0, 1
WATCHED = {EASY_SEAT: ('araat', 'aralode'), NORMAL_SEAT: ('tarcage', 'targarg')}
SECONDS = '120'
MAP = 'AI Circle'


def ban(units):
    return ''.join('limit %s 0\n' % u for u in units)


def hpi_read(hpitool, data, path):
    where = subprocess.run([str(hpitool), 'where', str(data), path], capture_output=True, text=True, check=True).stdout
    archive, entry = re.search(r'-> (.+?)!(.+)$', where.strip()).groups()
    return subprocess.run([str(hpitool), 'cat', str(data / archive), entry], capture_output=True, check=True).stdout


def pack(hpitool, folder, out):
    subprocess.run([str(hpitool), 'pack', str(folder), str(out)], check=True, capture_output=True)


def install(a, root, name, n_ai):
    """A scratch install over the real one, with the game pack my_ai and the decoys."""
    target = root / name
    target.mkdir()
    jersey = None
    for f in a.data.iterdir():
        low = f.name.lower()
        if low in ('overrides', 'mods', 'modcache', 'overridecache', 'mapcache', 'maps'):
            continue
        if low == 'jersey.hpi':   # a small sounds-only root archive: repacked below
            jersey = f
            continue
        (target / f.name).symlink_to(f, target_is_directory=f.is_dir())
    maps = target / 'Maps'
    maps.mkdir()
    for f in a.data.iterdir():
        if f.name.lower() == 'maps' and f.is_dir():
            for m in f.iterdir():
                (maps / m.name).symlink_to(m)
    decoy = ban(WATCHED[NORMAL_SEAT])
    # The base install: a root archive that also carries ai/n_ai.txt.
    base = root / (name + '-jersey')
    subprocess.run([str(a.hpitool), 'extract', str(jersey), str(base)], check=True, capture_output=True)
    (base / 'ai').mkdir()
    (base / 'ai' / 'n_ai.txt').write_text(decoy)
    pack(a.hpitool, base, target / jersey.name)
    # The map: Inner Circle under another name, its archive carrying ai/n_ai.txt.
    kmp = root / (name + '-kmp')
    (kmp / 'kmap').mkdir(parents=True)
    (kmp / 'ai').mkdir()
    (kmp / 'ai' / 'n_ai.txt').write_text(decoy)
    for ext in ('.tnt', '.ota'):
        (kmp / 'kmap' / (MAP + ext)).write_bytes(hpi_read(a.hpitool, a.data, 'maps/Inner Circle' + ext))
    pack(a.hpitool, kmp, maps / (MAP + '.kmp'))
    # The game pack: its ai/n_ai.txt is the one file read; a root n_ai.txt is not.
    game = target / 'mods' / 'game' / 'my_ai'
    (game / 'ai').mkdir(parents=True)
    (game / 'ai' / 'n_ai.txt').write_text(n_ai)
    (game / 'n_ai.txt').write_text(decoy)
    # A cosmetic pack's ai/ never reaches a game.
    skin = target / 'mods' / 'cosmetic' / 'skin' / 'ai'
    skin.mkdir(parents=True)
    (skin / 'n_ai.txt').write_text(decoy)
    return target


def play(a, root, name, n_ai):
    data = install(a, root, name, n_ai)
    work = root / (name + '-run')
    work.mkdir()
    roster = work / 'roster.jsonl'
    os.environ['TAK_ROSTER_DUMP'] = str(roster)   # the server reads it at its first sample
    os.environ['TAK_ROSTER_EVERY'] = '150'
    try:
        with server(a.server.resolve(), data, work, '--no-auth', '--seed', '1') as (port, _, process):
            settings = work / 'settings.ini'
            settings.write_text('mods.gamePack = ' + 'my_ai'.encode().hex() + '\n' +
                                'mods.cosmeticPack = ' + 'skin'.encode().hex() + '\n')
            env = dict(os.environ, HOME=str(work), XDG_DATA_HOME=str(work), XDG_CONFIG_HOME=str(work),
                       TAK_SETTINGS_FILE=str(settings), TAK_HEADLESS='1', TAK_SPEED='80',
                       TAK_MP_WATCH='1', TAK_MP_AIS='2', TAK_AI_LEVELS='1,2',
                       SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
            env.pop('TAK_ROSTER_DUMP')
            env.pop('TAK_AI_LEVEL', None)
            result = subprocess.run([str(a.client.resolve()), 'game', MAP, '--data', str(data),
                                     '--server', '127.0.0.1', '--serverport', str(port), '--mphost', '--time', SECONDS],
                                    env=env, capture_output=True, text=True, timeout=600)
            log = (work / 'log').read_text()
            assert result.returncode == 0 and 'hash=' in result.stdout + result.stderr, \
                result.stdout + result.stderr + '\n' + log
            assert process.poll() is None, log
    finally:
        os.environ.pop('TAK_ROSTER_DUMP', None)
        os.environ.pop('TAK_ROSTER_EVERY', None)
    built, initial, levels = {}, {}, {}
    for line in roster.read_text().splitlines():
        row = json.loads(line)
        if row['kind'] == 'meta':
            levels = {p['p']: p['level'] for p in row['players']}
            continue
        seen = {}
        for u in row['units']:
            seen.setdefault(u[1], set()).add(u[2])
        for seat, types in seen.items():
            if seat not in initial:
                initial[seat] = types
            built.setdefault(seat, set()).update(types - initial[seat])
    assert levels.get(EASY_SEAT) == 1 and levels.get(NORMAL_SEAT) == 2, levels
    if a.verbose:
        print(name, {seat: sorted(types) for seat, types in built.items()})
    return built, log


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--server', type=Path, required=True)
    p.add_argument('--client', type=Path, required=True)
    p.add_argument('--hpitool', type=Path, required=True)
    p.add_argument('--data', type=Path, required=True)
    p.add_argument('--verbose', action='store_true')
    a = p.parse_args()
    a.data = a.data.resolve()
    adjusted = "AI level 2 plays the built-in profile adjusted by the game mods' ai/n_ai.txt"
    with tempfile.TemporaryDirectory(prefix='tak-ai-mod-') as tmp:
        root = Path(tmp)
        control, log = play(a, root, 'control', '// a Normal profile that changes nothing\n')
        assert adjusted in log, log
        assert log.count(' plays the built-in profile adjusted') == 1, log   # Normal only
        for seat, units in WATCHED.items():
            missing = [u for u in units if u not in control.get(seat, set())]
            assert not missing, 'the control game\'s seat %d never built %s (built: %s)' % (
                seat, missing, sorted(control.get(seat, set())))
        modded, log = play(a, root, 'modded', ban(WATCHED[EASY_SEAT] + WATCHED[NORMAL_SEAT]))
        assert adjusted in log, log
        assert log.count(' plays the built-in profile adjusted') == 1, log
        banned = [u for u in WATCHED[NORMAL_SEAT] + WATCHED[EASY_SEAT] if u in modded.get(NORMAL_SEAT, set())]
        assert not banned, 'the Normal AI built %s despite the mod\'s ai/n_ai.txt' % banned
        missing = [u for u in WATCHED[EASY_SEAT] if u not in modded.get(EASY_SEAT, set())]
        assert not missing, 'the Easy AI stopped building %s: only Normal reads ai/n_ai.txt' % missing
        print('PASS: ai/n_ai.txt travels with the game mods to the Normal seat only (it never built %s; '
              'Easy still built %s); root, cosmetic, base install and map n_ai.txt are not read'
              % (', '.join(WATCHED[NORMAL_SEAT]), ', '.join(WATCHED[EASY_SEAT])))


if __name__ == '__main__':
    main()
