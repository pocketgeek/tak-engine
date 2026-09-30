#!/usr/bin/env python3
"""Exercise referee-owned campaign results over real authenticated sockets."""
import argparse
import contextlib
import hashlib
from pathlib import Path
import sqlite3
import struct
import tempfile
import time

import crusades_auth_network_test as auth
import crusades_battle_network_test as battle


def begin(port, fingerprint, stack):
    peers = [stack.enter_context(contextlib.closing(battle.Peer(port))) for _ in range(2)]
    for peer, user, side in zip(peers, ('Alice', 'Bob'), (1, 2)):
        peer.hello(fingerprint)
        peer.login(user, [])
        auth.request(peer, 2**64 - 1, side)
    alice, bob = peers
    identity, room, _, _ = battle.issue(alice)
    assert battle.result(bob)[0] == identity
    alice.receive('JoinResult')
    initial = battle.lobby(alice.receive('LobbyState'))
    battle.join(bob, room, True)
    for slot, peer in enumerate(peers):
        battle.map_ready(peer, room)
        values = initial[3][slot][0]
        peer.send('SlotUpdate', bytes([slot, 1, values[1], values[2], values[3], 1, values[5]]))
    while True:
        ready = battle.lobby(alice.receive('LobbyState'))
        if ready[4] and all(ready[3][i][0][4] for i in (0, 1)):
            break
    alice.send('StartGame')
    for peer in peers:
        peer.receive('GameStarting')
    return identity, peers


def result(root, identity, timeout=12):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
            row = db.execute('SELECT outcome,winner_account,replay_id,replay_digest '
                             'FROM verified_match_results WHERE battle_id=?', (identity,)).fetchone()
        if row is not None:
            return row
        time.sleep(.025)
    raise AssertionError('no authoritative result: ' + (root / 'server.log').read_text(errors='replace'))


def verify_replay(root, identity, digest, fingerprint):
    files = [path for path in root.rglob('*.takrep')
             if hashlib.sha256(path.read_bytes()).hexdigest() == digest]
    assert len(files) == 1, files
    with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
        payload = db.execute('SELECT payload FROM verified_match_results WHERE battle_id=?', (identity,)).fetchone()[0]
    saved = auth.Reader(payload)
    assert payload[:6] == b'TAKCR1'
    saved.pos = 7  # encoding magic and outcome
    assert saved.num('<B') == 1
    winner_size = saved.num('<I')
    saved.pos += winner_size
    final_tick, final_hash, data_hash = (saved.num('<Q') for _ in range(3))
    assert data_hash == fingerprint
    replay = auth.Reader(files[0].read_bytes())
    assert replay.data[:4] == b'TAKR'
    replay.pos = 4
    assert replay.num('<I') == 9
    assert replay.num('<I') == auth.VERSION
    replay.field()  # map
    assert replay.num('<B') == 1  # Crusades Balance
    replay.pos += 2 + 4 + 2 + 4 + 2  # options, cap, seed and format-6 flags
    replay.field(); replay.field()  # mission, engine version
    assert replay.num('<Q') == fingerprint
    replay.num('<B'); replay.field()  # sight, map digest
    slot_count = replay.num('<B')
    replay.pos += 5 * slot_count
    count = replay.num('<I')
    assert count == final_tick
    for tick in range(count):
        length = replay.num('<I')
        assert struct.unpack_from('<I', replay.data, replay.pos)[0] == tick
        replay.pos += length
    checks = [(replay.num('<I'), replay.num('<Q')) for _ in range(replay.num('<I'))]
    assert (final_tick - 1, final_hash) in checks
    assert replay.pos == len(replay.data)


def run_case(binary, data, scenario):
    with tempfile.TemporaryDirectory(prefix='tak-result-' + scenario + '-') as temporary:
        root = Path(temporary)
        (root / 'synthetic.campaign').write_text(
            'campaign 1 "synthetic" "Synthetic result test"\n'
            'territory 1 "One"\nmap 1 "Frey River Plain"\n')
        if scenario == 'replay-failure':
            (root / 'crusades-replays').write_text('not a directory')
        with auth.server(binary, data, root) as (port, fingerprint), contextlib.ExitStack() as stack:
            identity, peers = begin(port, fingerprint, stack)
            alice, bob = peers
            if scenario == 'invalid-client':
                alice.send('Loaded', struct.pack('<Q', 0))
            else:
                for peer in peers:
                    peer.send('Loaded', struct.pack('<Q', fingerprint))
                for peer in peers:
                    assert peer.receive('TickBundle').num('<I') == 0
                if scenario in ('victory', 'replay-failure'):
                    # A forged player byte cannot destroy the opponent's monarch.
                    command = struct.pack('<BBiiffB16s', 12, 0, 1, 0, 0, 0, 0, bytes(16))
                    bob.send('PlayerCommands', struct.pack('<I', 1) + command)
                    # Only its actual owner can issue that self-destruct order.
                    alice.send('PlayerCommands', struct.pack('<I', 1) + command)
                elif scenario == 'desync':
                    for peer in peers:
                        peer.send('StateHash', struct.pack('<IQ', 0, 0))
                    auth.request(bob)
                    alice.send('LeaveGame')
                elif scenario == 'forged-win':
                    alice.send('MissionOutcome', bytes([1]))
                elif scenario == 'resignation':
                    alice.send('LeaveGame')
                elif scenario == 'abandonment':
                    for peer in peers:
                        peer.close()
                else:
                    raise AssertionError(scenario)
            recorded = result(root, identity)
            outcome, winner, replay_id, replay_digest = recorded
            if scenario in ('victory', 'resignation'):
                assert outcome == (0 if scenario == 'victory' else 1), recorded
                assert winner == 'bob', recorded
                assert replay_id and replay_digest
                # The digest binds the actual persisted replay, not a claimed path.
                verify_replay(root, identity, replay_digest, fingerprint)
            else:
                assert outcome not in (0, 1) and winner is None, recorded
                if scenario == 'invalid-client':
                    assert outcome == 8, recorded
                if scenario == 'forged-win':
                    assert outcome == 8, recorded
                if scenario == 'replay-failure':
                    assert outcome == 4 and replay_id is None and replay_digest is None, recorded
                if scenario == 'desync':
                    assert outcome in (6, 7), recorded
            # A client-sent server-only result/invitation message cannot change it.
            if scenario not in ('abandonment', 'invalid-client'):
                bob.send('CrusadesBattleResult', bytes([0]) + auth.field('I won this territory'))
                auth.request(bob)
                assert result(root, identity) == recorded
            with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
                assert db.execute('SELECT count(*) FROM verified_match_results').fetchone()[0] == 1
                assert db.execute('SELECT revision FROM campaigns').fetchone()[0] == 0
                assert db.execute('SELECT count(*) FROM campaign_events').fetchone()[0] == 1
                status = db.execute('SELECT e.status FROM issued_battles b JOIN battle_status_events e '
                                    'ON e.battle_id=b.id AND e.revision=b.revision WHERE b.id=?', (identity,)).fetchone()[0]
                assert status == (3 if outcome in (0, 1) else 2)
        print('PASS: authoritative campaign result ' + scenario)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    args = parser.parse_args()
    for case in ('victory', 'resignation', 'desync', 'invalid-client', 'abandonment', 'forged-win', 'replay-failure'):
        run_case(args.server.resolve(), args.data.resolve(), case)
