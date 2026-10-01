#!/usr/bin/env python3
"""Actual campaign history -> retained replay -> campaign return through SDL.

Runs only software/dummy SDL in isolated preferences. Creates a real verified
battle with synthetic accounts, then watches it as a different enrolled account.
"""
import argparse
import contextlib
import hashlib
from pathlib import Path
import re
import sqlite3
import struct
import sys
import tempfile

import crusades_auth_network_test as auth
import crusades_battle_network_test as battle
import crusades_result_network_test as results
import crusades_ui_network_test as ui


def verified_battle(port, fingerprint, root):
    with contextlib.ExitStack() as stack:
        identity, peers = results.begin(port, fingerprint, stack)
        alice, bob = peers
        for peer in peers:
            peer.send('Loaded', struct.pack('<Q', fingerprint))
        for peer in peers:
            assert peer.receive('TickBundle').num('<I') == 0
        command = struct.pack('<BBiiffB16s', 12, 0, 1, 0, 0, 0, 0, bytes(16))
        alice.send('PlayerCommands', struct.pack('<I', 1) + command)
        outcome, winner, replay_id, digest = results.result(root, identity)
        assert outcome == 0 and winner == 'bob' and replay_id and digest
        results.verify_replay(root, identity, digest, fingerprint)
    with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
        row = db.execute('SELECT payload FROM verified_match_results WHERE battle_id=?', (identity,)).fetchone()
        data = auth.Reader(row[0]); data.pos = 7
        assert data.num('<B') == 1
        winner_size = data.num('<I')
        data.pos += winner_size
        final_tick, final_hash = data.num('<Q'), data.num('<Q')
    return identity, digest, final_tick, final_hash


def snapshot_db(root):
    with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
        # Compare durable contents, rather than only row counts: a Watch action
        # must not change credit, enrollment, battle state or existing metadata.
        tables = ('campaigns', 'campaign_events', 'campaign_participants',
                  'allegiance_events', 'issued_battles', 'battle_status_events',
                  'battle_rooms', 'verified_match_results', 'battle_results',
                  'rule_decisions', 'battle_participants', 'territory_battle_history')
        return tuple((table, db.execute('SELECT * FROM ' + table + ' ORDER BY rowid').fetchall())
                     for table in tables)


def migrate_fixture_to_protocol(root, identity, digest, protocol):
    """Test-admin fixture: give an archive an incompatible simulation version.

    Only its protocol header and corresponding content-addressed
    identity change. This deliberately bypasses the result immutability trigger
    inside one transaction, restoring its exact definition before committing.
    Production APIs never permit changing a verified result.
    """
    artifacts = [path for path in (root / 'crusades-replays').glob('*.takrep')
                 if hashlib.sha256(path.read_bytes()).hexdigest() == digest]
    assert len(artifacts) == 1, artifacts
    original = artifacts[0]
    original_bytes = original.read_bytes()
    recording = bytearray(original_bytes)
    assert recording[:4] == b'TAKR'
    assert struct.unpack_from('<II', recording, 4) == (9, auth.VERSION)
    struct.pack_into('<I', recording, 8, protocol)
    replacement_digest = hashlib.sha256(recording).hexdigest()
    with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
        replay_id, payload, outcome, winner = db.execute(
            'SELECT replay_id,payload,outcome,winner_account FROM verified_match_results WHERE battle_id=?',
            (identity,)).fetchone()
        replacement_id = replay_id.replace(digest, replacement_digest)
        assert replay_id != replacement_id
        assert payload.count(replay_id.encode()) == 1 and payload.count(digest.encode()) == 2
        replacement_payload = payload.replace(replay_id.encode(), replacement_id.encode()).replace(
            digest.encode(), replacement_digest.encode())
        assert len(replacement_payload) == len(payload)
        trigger = db.execute("SELECT sql FROM sqlite_master WHERE type='trigger' AND name='verified_results_no_update'").fetchone()[0]
        replacement = original.with_name(replacement_id)
        replacement.write_bytes(recording)
        try:
            db.execute('BEGIN IMMEDIATE')
            db.execute('DROP TRIGGER verified_results_no_update')
            db.execute('UPDATE verified_match_results SET replay_id=?,replay_digest=?,payload=? WHERE battle_id=?',
                       (replacement_id, replacement_digest, replacement_payload, identity))
            db.execute(trigger)
            assert db.execute('SELECT outcome,winner_account FROM verified_match_results WHERE battle_id=?',
                              (identity,)).fetchone() == (outcome, winner)
            db.commit()
        except BaseException:
            db.rollback(); replacement.unlink(missing_ok=True)
            raise
        original.unlink()
    assert recording[:8] == original_bytes[:8] and recording[12:] == original_bytes[12:]
    return replacement_digest


def watch(client, data, root, port, name, expect_return=True):
    # The history response and replay request need ordinary network frame time.
    # Subsequent no-op clicks keep capture alive while the real replay finishes.
    clicks = ';'.join(['40,135', '550,100'] + ['600,420'] * 20 + ['500,363'] + ['600,420'] * 220)
    process, log, shot = ui.launch(client, data, root, port, name, clicks, 2500,
                                  user='Carol', press='Escape' if expect_return else None)
    try:
        ui.finish(process, log, shot)
    finally:
        if process.poll() is None:
            process.kill(); process.wait()
        log.close()
    text = shot.with_suffix('.log').read_text(errors='replace')
    if expect_return:
        assert 'watching retained replay without joining a battle' in text, text[-5000:]
        assert 'returned from read-only replay' in text, text[-5000:]
        assert 'DIVERGED' not in text, text[-5000:]
        assert 'audio: 1 output channels' not in text and 'NO DEVICE' not in text, 'Watch lost exclusive SDL audio output: ' + text[-5000:]
        assert text.count('audio: 2 output channels\n') >= 3, 'original, replay and restored audio outputs were not opened'
    else:
        assert 'watching retained replay without joining a battle' not in text, 'missing replay unexpectedly opened'
    return text, shot


def run(server, client, data, root):
    root.mkdir(parents=True, exist_ok=True)
    # Give the observer installed retail resources, but no pre-existing map
    # package cache from a participant or another test. The ordinary replay
    # loader must rebuild the known stock package and verify its recorded digest.
    observer_data = root / 'observer-data'
    observer_data.mkdir()
    for resource in data.iterdir():
        if resource.name not in ('MapCache', 'ReplayCache'):
            (observer_data / resource.name).symlink_to(resource, target_is_directory=resource.is_dir())
    assert not (observer_data / 'MapCache').exists()
    (root / 'synthetic.campaign').write_text(
        'campaign 1 "synthetic" "Synthetic archive UI test"\n'
        'territory 1 "One"\nmap 1 "Frey River Plain"\nterritory 2 "Two"\n')
    with auth.server(server, data, root) as (port, fingerprint):
        identity, digest, final_tick, final_hash = verified_battle(port, fingerprint, root)
        # Carol never participates in that battle. Public archive access requires
        # campaign enrollment, rather than knowledge of an active private token.
        with contextlib.closing(battle.Peer(port)) as carol:
            carol.hello(fingerprint); carol.login('Carol', [])
            auth.request(carol, 2**64 - 1, 1)
        before = snapshot_db(root)
        text, shot = watch(client, observer_data, root, port, 'history-watch-return')
        returned = re.search(r'replay return tick=(\d+)/(\d+) hash=([0-9a-f]+)', text)
        assert returned, text[-5000:]
        assert int(returned[1]) == int(returned[2]) == final_tick, returned.groups()
        assert int(returned[3], 16) == final_hash, 'ordinary replay differs from authoritative archive'
        assert list((observer_data / 'MapCache').glob('*.takmap')), 'installed map fallback did not verify/cache the replay map'
        assert snapshot_db(root) == before, 'watching changed campaign/battle/result state'
        # A simulation correction changes replay results even if the command
        # layout is unchanged. Older recordings must remain listed in history
        # without offering playback using the new simulation.
        migrate_fixture_to_protocol(root, identity, digest, auth.VERSION-1)
        before = snapshot_db(root)
        watch(client, observer_data, root, port, 'history-incompatible-replay', False)
        assert snapshot_db(root) == before, 'incompatible replay changed durable history'
        # Remove retained bytes without editing the durable archive. Also remove
        # the client's complete cache so this run genuinely exercises absence.
        artifacts = list(root.rglob('*.takrep'))
        assert artifacts
        for path in artifacts:
            path.unlink()
        missing_text, _ = watch(client, observer_data, root, port, 'history-missing-replay', False)
        assert snapshot_db(root) == before, 'missing replay erased/changed history'
        server_text = (root / 'server.log').read_text(errors='replace')
        assert len(re.findall("'Carol' joined lobby", server_text)) == 4, 'Watch caused reconnect/login instead of preserving session'
    print('PASS: enrolled nonparticipant history, current-protocol SDL retained replay, exact final hash, campaign return, incompatible and missing-file metadata retention')
    print('Artifacts:', root)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith('linux'):
        parser.error('actual SDL smoke requires Linux isolated preferences')
    if args.artifacts:
        run(args.server.resolve(), args.client.resolve(), args.data.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='tak-crusades-history-ui-') as temporary:
            run(args.server.resolve(), args.client.resolve(), args.data.resolve(), Path(temporary))
