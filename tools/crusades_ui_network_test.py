#!/usr/bin/env python3
"""Actual Debug takclient/GameView SDL events against a real campaign server.

Uses SDL's dummy video/software renderer: never controls the user's desktop.
Retail resources stay local. Screenshots are optional test artifacts, not assets.
"""
import argparse
import contextlib
import os
from pathlib import Path
import re
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time

import crusades_auth_network_test as auth
import crusades_battle_network_test as battle

PASSWORD = 'local synthetic campaign test password'


def until(predicate, timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = predicate()
        if value:
            return value
        time.sleep(.04)
    raise AssertionError('timed out waiting for actual UI/server state')


def allegiance(root):
    with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
        return db.execute("SELECT e.alliance,e.revision FROM campaign_participants p "
                          "JOIN allegiance_events e ON e.campaign_id=p.campaign_id "
                          "AND e.account_id=p.account_id AND e.revision=p.revision "
                          "WHERE p.account_id='alice'").fetchone()


def launch(client, data, root, port, name, clicks, delay=2500, user='Alice', press=None):
    pref = root / 'preferences' / 'TAKengine' / 'TAKingdoms'
    pref.mkdir(parents=True, exist_ok=True)
    (pref / 'settings.ini').write_text('fullscreen = false\nvsync = false\nmaxFps = 30\n')
    environment = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       SDL_RENDER_DRIVER='software', XDG_DATA_HOME=str(root / 'preferences'),
                       TAK_SHOT_CRUSADES='synthetic', TAK_SHOT_MS=str(delay),
                       TAK_SHOT_CLICKS=clicks)
    # Suppress unrelated inherited development drivers.
    for key in tuple(environment):
        if key.startswith('TAK_') and key not in ('TAK_SHOT_CRUSADES', 'TAK_SHOT_MS', 'TAK_SHOT_CLICKS'):
            del environment[key]
    if press:
        environment['TAK_SHOT_PRESS'] = press
        environment['TAK_SHOT_PRESS_AFTER_CLICKS'] = '1'
    shot = root / (name + '.png')
    log = (root / (name + '.log')).open('w')
    process = subprocess.Popen([str(client), 'game', 'Frey River Plain', '--data', str(data),
                                '--server', '127.0.0.1', '--serverport', str(port),
                                '--user', user, '--pass', PASSWORD, '--winsize', '960', '540',
                                '--maxfps', '30', '--novsync', '--shot', str(shot)],
                               stdout=log, stderr=log, env=environment)
    return process, log, shot


def finish(process, log, shot):
    try:
        code = process.wait(timeout=60)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        raise
    finally:
        log.close()
    assert code == 0, shot.with_suffix('.log').read_text(errors='replace')[-5000:]
    image = shot.read_bytes()
    assert image[:8] == b'\x89PNG\r\n\x1a\n'
    assert struct.unpack('>II', image[16:24]) == (960, 540), 'unexpected UI viewport'
    assert len(image) > 1000, 'empty UI capture'


def run(server, client, data, root):
    root.mkdir(parents=True, exist_ok=True)
    (root / 'synthetic.campaign').write_text(
        'campaign 1 "synthetic" "Synthetic UI test"\n'
        'territory 1 "One"\nmap 1 "Frey River Plain"\nterritory 2 "Two"\n')
    with auth.server(server, data, root) as (port, fingerprint):
        # No raw allegiance request for Alice: these mutations must originate
        # in the real screen's SDL mouse handler through real MpClient auth.
        process, log, shot = launch(client, data, root, port, 'join-honor', '55,463')
        finish(process, log, shot)
        until(lambda: allegiance(root) == (1, 0))
        process, log, shot = launch(client, data, root, port, 'switch-terror', '150,463')
        finish(process, log, shot)
        until(lambda: allegiance(root) == (2, 1))
        # Reconnect the actual UI and invite it from a second authenticated
        # participant. SDL clicks select the card and join the issued room.
        with contextlib.closing(battle.Peer(port)) as bob:
            bob.hello(fingerprint)
            bob.login('Bob', [])
            auth.request(bob, 2**64 - 1, 1)
            old_text = (root / 'server.log').read_text(errors='replace')
            count = len(re.findall("'Alice' joined lobby", old_text))
            process, log, shot = launch(client, data, root, port, 'join-invitation', '300,358;700,478', 6000)
            try:
                until(lambda: len(re.findall("'Alice' joined lobby", (root / 'server.log').read_text(errors='replace'))) > count)
                identity, room, _, _ = battle.issue(bob, opponent='Alice')
                finish(process, log, shot)
                text = (root / 'server.log').read_text(errors='replace')
                assert re.search(r'client \d+ joined game ' + str(room) + r' at slot 1', text), 'UI did not join invitation'
                with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
                    assert db.execute('SELECT count(*) FROM issued_battles WHERE id=?', (identity,)).fetchone()[0] == 1
                    assert db.execute('SELECT revision FROM campaigns').fetchone()[0] == 0, 'UI invented strategic progress'
                assert allegiance(root) == (2, 1), 'reconnect changed allegiance'
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                log.close()
    print('PASS: actual GameView campaign capture, SDL allegiance join/switch, authenticated reconnect and invitation join')
    print('Artifacts:', root)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--client', type=Path, required=True, help='Debug takclient with TAK_SHOT_CRUSADES')
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith('linux'):
        parser.error('actual UI smoke currently requires Linux for isolated SDL preferences')
    if args.artifacts:
        run(args.server.resolve(), args.client.resolve(), args.data.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='tak-crusades-ui-live-') as temporary:
            run(args.server.resolve(), args.client.resolve(), args.data.resolve(), Path(temporary))
