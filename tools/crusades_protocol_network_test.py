#!/usr/bin/env python3
"""Versioned strategic traffic against a real authenticated campaign server."""
import argparse
import contextlib
from pathlib import Path
import sqlite3
import struct
import tempfile

import crusades_auth_network_test as auth
import crusades_battle_network_test as battle
import crusades_result_network_test as results

ABSENT = 2**64 - 1


def request(peer, kind, identity, extra=b'', version=3):
    peer.send(kind, struct.pack('<HI', version, identity) + extra)


def response(peer, kind, identity):
    # Unsolicited lifecycle updates may precede an explicit query response.
    while True:
        r = peer.receive(kind)
        assert r.num('<H') == 3
        actual = r.num('<I')
        if actual == identity:
            return r
        assert actual == 0, (actual, identity)


def error(peer, identity, code):
    r = response(peer, 'CrusadesError', identity)
    assert r.num('<B') == code
    assert r.field() == b''
    assert r.num('<B') == 0
    assert r.field()
    assert r.pos == len(r.data)


def optional_string(r):
    flag = r.num('<B')
    assert flag in (0, 1)
    return r.field() if flag else None


def snapshot(peer, identity, revision, activity=(0, 0)):
    r = response(peer, 'CrusadesCampaignSnapshot', identity)
    assert r.field() == b'synthetic'
    assert r.field() == b'Synthetic network test'
    assert r.num('<Q') == revision
    assert r.field() == b'historical-darien-v1'
    assert r.num('<H') == 2
    for territory in (1, 2):
        assert r.num('<I') == territory
        assert r.field() == (b'One' if territory == 1 else b'Two')
        assert optional_string(r) is None  # Unknown native faction.
        assert optional_string(r) is None  # Unknown terrain.
        assert optional_string(r) == (b'Frey River Plain' if territory == 1 else None)
        assert r.num('<B') == 0  # Unknown neighbor graph is not isolated.
        assert r.num('<B') == 0  # Unknown owner is not contested.
        assert optional_string(r) is None
        assert all(r.num('<B') == 0 for _ in range(7))
        assert r.num('<B') == 1  # Authoritative runtime activity, including zeros.
        assert (r.num('<I'), r.num('<I')) == (activity if territory == 1 else (0, 0))
    assert r.pos == len(r.data)


def status(peer, identity, battle_id, phase, room=None, outcome=None):
    r = response(peer, 'CrusadesBattleStatus', identity)
    assert r.field() == b'synthetic'
    assert r.field() == battle_id.encode()
    assert r.num('<Q') == 1
    assert r.num('<I') == 1
    assert r.num('<B') == phase
    assert r.field() == b'Frey River Plain'
    assert r.num('<Q') > 0
    actual_room = r.num('<I')
    if room is not None:
        assert actual_room == room
    present = r.num('<B')
    assert present == (outcome is not None)
    if present:
        assert r.num('<B') == outcome
        assert r.num('<Q') > 0
        r.num('<Q')
        count = r.num('<B')
        assert count == 1
        assert r.field() == b'alice'
    assert r.pos == len(r.data)


def get_snapshot(peer, identity, revision=ABSENT):
    request(peer, 'CrusadesGetSnapshot', identity, auth.field('synthetic') + struct.pack('<Q', revision))


def player(peer, identity, revision, alliance=None, last_battle=None):
    r = response(peer, 'CrusadesPlayerStatus', identity)
    assert r.field() == b'synthetic'
    assert r.num('<Q') == revision
    present = r.num('<B')
    assert present == (alliance is not None)
    if present:
        assert r.num('<B') == alliance
        assert r.num('<Q') == 0
        joined, changed = r.num('<Q'), r.num('<Q')
        assert joined > 0 and changed >= joined
    count = r.num('<B')
    assert count == (last_battle is not None)
    if count:
        assert r.field() == last_battle.encode()
        assert r.num('<I') == 1
        assert r.num('<B') == 3
    assert r.num('<B') == 0
    assert r.pos == len(r.data)


def advance_revision(root):
    # A trusted test-admin transaction exercises external persisted updates;
    # clients have no campaign mutation packet. No ownership formula is invented.
    with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
        db.execute('BEGIN IMMEDIATE')
        db.execute("INSERT INTO campaign_events SELECT campaign_id,1,'TestAdmin',NULL,snapshot "
                   "FROM campaign_events WHERE campaign_id='synthetic' AND revision=0")
        db.execute("UPDATE campaigns SET revision=1 WHERE id='synthetic'")
        db.commit()


def run(binary, data):
    with tempfile.TemporaryDirectory(prefix='tak-campaign-protocol-') as temporary:
        root = Path(temporary)
        (root / 'synthetic.campaign').write_text(
            'campaign 1 "synthetic" "Synthetic network test"\n'
            'territory 1 "One"\nmap 1 "Frey River Plain"\nterritory 2 "Two"\n')
        secrets = []
        with auth.server(binary, data, root) as (port, fingerprint), contextlib.ExitStack() as stack:
            alice, bob, other = [stack.enter_context(contextlib.closing(battle.Peer(port))) for _ in range(3)]
            request(alice, 'CrusadesListCampaigns', 1, auth.field('') + struct.pack('<H', 64))
            error(alice, 1, 3)
            for peer, user in zip((alice, bob, other), ('Alice', 'Bob', 'Other')):
                peer.hello(fingerprint)
                peer.login(user, secrets)
            request(alice, 'CrusadesListCampaigns', 2, auth.field('') + struct.pack('<H', 64))
            r = response(alice, 'CrusadesCampaignList', 2)
            assert r.num('<H') == 1
            assert r.field() == b'synthetic' and r.field() == b'Synthetic network test'
            assert r.num('<Q') == 0 and r.field() == b'historical-darien-v1'
            assert r.field() == b'' and r.pos == len(r.data)
            for identity, payload in [(3, b''), (4, auth.field('synthetic') + struct.pack('<Q', ABSENT) + b'x'),
                                      (5, auth.field('synthetic') + struct.pack('<Q', ABSENT - 1))]:
                request(alice, 'CrusadesGetSnapshot', identity, payload)
                error(alice, identity, 1)
            request(alice, 'CrusadesGetSnapshot', 6, auth.field('synthetic') + struct.pack('<Q', ABSENT), version=4)
            error(alice, 6, 2)
            request(alice, 'CrusadesGetSnapshot', 7, auth.field('unknown') + struct.pack('<Q', ABSENT))
            error(alice, 7, 5)
            get_snapshot(alice, 8)
            snapshot(alice, 8, 0)
            get_snapshot(bob, 9)
            snapshot(bob, 9, 0)
            request(alice, 'CrusadesGetPlayerStatus', 10, auth.field('synthetic'))
            player(alice, 10, 0)
            advance_revision(root)
            snapshot(alice, 0, 1)  # Live subscription corrected from persisted state.
            snapshot(bob, 0, 1)
            for identity, revision in ((11, 0), (12, 999)):
                get_snapshot(alice, identity, revision)
                snapshot(alice, identity, 1)
            auth.request(alice, ABSENT, 1)
            auth.request(bob, ABSENT, 2)
            identity, room, _, _ = battle.issue(alice)
            assert battle.result(bob)[0] == identity
            for peer in (alice, bob):
                status(peer, 0, identity, 0, room)
            request(other, 'CrusadesGetBattleStatus', 13, auth.field(identity))
            error(other, 13, 5)
            request(other, 'CrusadesGetBattleStatus', 14, auth.field('unknown'))
            error(other, 14, 5)
            get_snapshot(other, 30)
            snapshot(other, 30, 1, (1, 0))
            other.send('ListGames')
            assert other.receive('GameList').num('<I') == 0  # Private campaign invitation, not public skirmish.
            alice.receive('JoinResult')
            initial = battle.lobby(alice.receive('LobbyState'))
            battle.join(bob, room, True)
            for slot, peer in enumerate((alice, bob)):
                battle.map_ready(peer, room)
                values = initial[3][slot][0]
                peer.send('SlotUpdate', bytes([slot, 1, values[1], values[2], values[3], 1, values[5]]))
            while True:
                ready = battle.lobby(alice.receive('LobbyState'))
                if ready[4] and all(ready[3][i][0][4] for i in (0, 1)):
                    break
            alice.send('StartGame')
            for peer in (alice, bob):
                status(peer, 0, identity, 1, room)
                peer.receive('GameStarting')
                peer.send('Loaded', struct.pack('<Q', fingerprint))
            for peer in (alice, bob):
                assert peer.receive('TickBundle').num('<I') == 0
            get_snapshot(other, 31)
            snapshot(other, 31, 1, (0, 1))
            request(alice, 'CrusadesGetBattleStatus', 15, auth.field(identity))
            status(alice, 15, identity, 1, room)
            bob.send('LeaveGame')
            saved = results.result(root, identity)
            assert saved[0] == 1 and saved[1] == 'alice'
            for peer in (alice, bob):
                status(peer, 0, identity, 3, 0, 1)
            get_snapshot(other, 32)
            snapshot(other, 32, 1, (0, 0))
            results.verify_replay(root, identity, saved[3], fingerprint)
        # Real process restart: fresh snapshots, same account, durable results,
        # no stale runtime room association or leaked single-use launch token.
        with auth.server(binary, data, root) as (port, fingerprint), contextlib.closing(battle.Peer(port)) as alice:
            alice.hello(fingerprint)
            alice.login('ALICE', secrets)
            get_snapshot(alice, 16)
            snapshot(alice, 16, 1)
            request(alice, 'CrusadesGetPlayerStatus', 17, auth.field('synthetic'))
            player(alice, 17, 1, 1, identity)
            request(alice, 'CrusadesGetBattleStatus', 18, auth.field(identity))
            status(alice, 18, identity, 3, 0, 1)
        with auth.server(binary, data, root, enabled=False) as (port, fingerprint), contextlib.closing(battle.Peer(port)) as alice:
            alice.hello(fingerprint)
            alice.login('Alice', secrets)
            request(alice, 'CrusadesListCampaigns', 19, auth.field('') + struct.pack('<H', 64))
            error(alice, 19, 4)
    print('PASS: live versioning, authenticated snapshots, subscription/revision recovery, participant isolation, lifecycle and durable reconnect')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    args = parser.parse_args()
    run(args.server.resolve(), args.data.resolve())
