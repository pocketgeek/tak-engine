#!/usr/bin/env python3
"""Our levels' ai/n.txt: a game mod's (through the real mod transfer) or the map's (in
its .kmp, through the room's map package) reaches the server's Normal AI.

Headless all-AI games on AI Circle (Inner Circle's map in a single-map archive under
Maps/), seed 1, a spectating host: seat 0 Aramon on Easy, seat 1 Taros on Normal.
Every game ticks a game pack, uploaded with the room's game mods and applied by the
server (the only place the AI runs). Misplaced n.txt files say `limit <unit> 0` for
the Normal seat's watched units in every game -- at the game pack's root, in a
cosmetic pack's ai/ and in a base install archive's ai/ -- and are never read.

  control  the map's ai/n.txt bans the Normal seat's watched units, but the game
           pack's ai/n.txt (a comment only) wins over it: the Normal AI builds them
  map      the game pack has no ai/n.txt; the map's bans both seats' watched units:
           the Normal AI never builds its own, the Easy AI still builds its
  modded   the game pack's ai/n.txt bans both seats' watched units (the map's says
           nothing): the same
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


def install(a, root, name, mod_n, map_n):
    """A scratch install over the real one, with the game pack my_ai and the decoys.
    mod_n / map_n: the game pack's / the map archive's ai/n.txt (None = no such file)."""
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
    # The base install: a root archive that also carries ai/n.txt.
    base = root / (name + '-jersey')
    subprocess.run([str(a.hpitool), 'extract', str(jersey), str(base)], check=True, capture_output=True)
    (base / 'ai').mkdir()
    (base / 'ai' / 'n.txt').write_text(decoy)
    pack(a.hpitool, base, target / jersey.name)
    # The map: Inner Circle under another name, its archive maybe carrying ai/n.txt.
    kmp = root / (name + '-kmp')
    (kmp / 'kmap').mkdir(parents=True)
    if map_n is not None:
        (kmp / 'ai').mkdir()
        (kmp / 'ai' / 'n.txt').write_text(map_n)
    for ext in ('.tnt', '.ota'):
        (kmp / 'kmap' / (MAP + ext)).write_bytes(hpi_read(a.hpitool, a.data, 'maps/Inner Circle' + ext))
    pack(a.hpitool, kmp, maps / (MAP + '.kmp'))
    # The game pack: only its ai/n.txt is a level file; a root n.txt is not.
    game = target / 'mods' / 'game' / 'my_ai'
    game.mkdir(parents=True)
    if mod_n is not None:
        (game / 'ai').mkdir()
        (game / 'ai' / 'n.txt').write_text(mod_n)
    (game / 'n.txt').write_text(decoy)
    # A cosmetic pack's ai/ never reaches a game.
    skin = target / 'mods' / 'cosmetic' / 'skin' / 'ai'
    skin.mkdir(parents=True)
    (skin / 'n.txt').write_text(decoy)
    return target


def play(a, root, name, mod_n, map_n):
    data = install(a, root, name, mod_n, map_n)
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
    from_mod = "AI level 2 plays the built-in profile adjusted by the game mods' ai/n.txt"
    from_map = "AI level 2 plays the built-in profile adjusted by the map package's ai/n.txt"
    nothing = '// a Normal profile that changes nothing\n'
    both = ban(WATCHED[EASY_SEAT] + WATCHED[NORMAL_SEAT])

    def obeyed(built, name):
        banned = [u for u in WATCHED[NORMAL_SEAT] + WATCHED[EASY_SEAT] if u in built.get(NORMAL_SEAT, set())]
        assert not banned, 'the Normal AI built %s despite the %s ai/n.txt' % (banned, name)
        missing = [u for u in WATCHED[EASY_SEAT] if u not in built.get(EASY_SEAT, set())]
        assert not missing, 'the Easy AI stopped building %s: only Normal reads ai/n.txt' % missing

    with tempfile.TemporaryDirectory(prefix='tak-ai-mod-') as tmp:
        root = Path(tmp)
        control, log = play(a, root, 'control', nothing, ban(WATCHED[NORMAL_SEAT]))
        assert from_mod in log, log
        assert log.count(' plays the built-in profile adjusted') == 1, log   # Normal only
        for seat, units in WATCHED.items():
            missing = [u for u in units if u not in control.get(seat, set())]
            assert not missing, 'the control game\'s seat %d never built %s (built: %s)' % (
                seat, missing, sorted(control.get(seat, set())))
        mapped, log = play(a, root, 'map', None, both)
        assert from_map in log, log
        assert log.count(' plays the built-in profile adjusted') == 1, log
        obeyed(mapped, "map's")
        modded, log = play(a, root, 'modded', both, nothing)
        assert from_mod in log, log
        assert log.count(' plays the built-in profile adjusted') == 1, log
        obeyed(modded, "mod's")
        print('PASS: ai/n.txt from the game mods or the map package reaches the Normal seat only (it never '
              'built %s; Easy still built %s); a game mod\'s wins over the map\'s; root, cosmetic and base '
              'install n.txt are not read' % (', '.join(WATCHED[NORMAL_SEAT]), ', '.join(WATCHED[EASY_SEAT])))

if __name__ == '__main__':
    main()
