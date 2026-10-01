#!/usr/bin/env python3
"""Modern Crusades rendezvous against the actual authenticated server.

Synthetic accounts/definitions only. Optional Debug SDL smoke uses software
rendering and isolated Linux preferences; it never controls the user's desktop.
"""
import argparse
import contextlib
from pathlib import Path
import sqlite3
import struct
import sys
import tempfile
import time

import crusades_auth_network_test as auth
import crusades_battle_network_test as battle
import crusades_result_network_test as results

WIRE_VERSION = 4
ABSENT = 2**64 - 1


def request(peer, kind, identity, campaign='synthetic', territory=None, extra=b'', version=WIRE_VERSION):
    payload = struct.pack('<HI', version, identity) + auth.field(campaign)
    if territory is not None:
        payload += struct.pack('<I', territory)
    peer.send(kind, payload + extra)


def response(peer, kind, identity):
    while True:
        r = peer.receive(kind)
        assert r.num('<H') == WIRE_VERSION
        actual = r.num('<I')
        if actual == identity:
            return r
        assert actual == 0, (actual, identity)


def error(peer, identity, code):
    r = response(peer, 'CrusadesError', identity)
    assert r.num('<B') == code
    r.field()  # Sanitized campaign scope, without another player's identity.
    if r.num('<B'):
        r.num('<Q')
    assert r.field()
    assert r.pos == len(r.data)


def board(peer, identity):
    r = response(peer, 'CrusadesMatchmakingStatus', identity)
    assert r.field() == b'synthetic'
    revision, generation = r.num('<Q'), r.num('<Q')
    can_search = r.num('<B')
    assert can_search in (0, 1)
    present = r.num('<B')
    assert present in (0, 1)
    searching, expires = (r.num('<I'), r.num('<Q')) if present else (None, None)
    territories = {}
    for _ in range(r.num('<H')):
        territory = r.num('<I')
        assert territory not in territories
        eligible = r.num('<B')
        assert eligible in (0, 1)
        territories[territory] = (eligible, *(r.num('<I') for _ in range(4)))
    assert set(territories) == {1, 2, 3}
    assert r.pos == len(r.data), 'board contains unadvertised identities or secrets'
    assert revision == 0
    return dict(generation=generation, can_search=bool(can_search), searching=searching,
                expires=expires, territories=territories)


def read_board(peer, identity):
    request(peer, 'CrusadesGetMatchmaking', identity)
    return board(peer, identity)


def search(peer, identity, territory=1):
    request(peer, 'CrusadesSearchBattle', identity, territory=territory)
    return board(peer, identity)


def cancel(peer, identity):
    request(peer, 'CrusadesCancelSearch', identity)
    return board(peer, identity)


def receive_lobby(peer, room, kind='LobbyState'):
    # Peer retains unmatched message kinds. Earlier room snapshots may remain
    # queued after cancellation; never let them supply a new battle's slots.
    for _ in range(128):
        state = battle.lobby(peer.receive(kind))
        if state[0] == room:
            return state
        assert state[0] < room, 'received a future/unexpected room snapshot'
    raise AssertionError('too many stale room snapshots')


def receive_map_offer(peer, room):
    for _ in range(128):
        r = peer.receive('MapOffer')
        offered_room = r.num('<I')
        map_id, digest, size = r.field(), r.field(), r.num('<I')
        assert r.pos == len(r.data)
        if offered_room == room:
            assert map_id == b'Frey River Plain' and len(digest) == 64 and size > 0
            return digest
        assert offered_room < room, 'received a future/unexpected map offer'
    raise AssertionError('too many stale map offers')


def map_ready(peer, room):
    digest = receive_map_offer(peer, room)
    peer.send('MapReady', struct.pack('<I', room) + auth.field(digest))


def definition(root):
    (root / 'synthetic.campaign').write_text(
        'campaign 1 "synthetic" "Synthetic matchmaking test"\n'
        'territory 1 "Available"\nmap 1 "Frey River Plain"\n'
        'territory 2 "No authored map"\n'
        'territory 3 "Missing installed map"\nmap 3 "Not an installed map"\n')


def log_in(stack, port, fingerprint, user, secrets, side=None):
    peer = stack.enter_context(contextlib.closing(battle.Peer(port)))
    peer.hello(fingerprint)
    peer.login(user, secrets)
    if side is not None:
        auth.request(peer, ABSENT, side)
    return peer


def wait_board(peer, identity, predicate, timeout=5):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        current = read_board(peer, identity)
        if predicate(current):
            return current
        time.sleep(.1)
    raise AssertionError('authoritative board did not converge')


def run(server, data):
    with tempfile.TemporaryDirectory(prefix='tak-crusades-matching-') as temporary:
        root = Path(temporary)
        definition(root)
        secrets = []
        with auth.server(server, data, root) as (port, fingerprint), contextlib.ExitStack() as stack:
            anonymous = stack.enter_context(contextlib.closing(battle.Peer(port)))
            request(anonymous, 'CrusadesGetMatchmaking', 1)
            error(anonymous, 1, 3)
            alice = log_in(stack, port, fingerprint, 'Alice', secrets, 1)
            bob = log_in(stack, port, fingerprint, 'Bob', secrets, 2)
            carol = log_in(stack, port, fingerprint, 'Carol', secrets, 1)
            dave = log_in(stack, port, fingerprint, 'Dave', secrets, 2)
            visitor = log_in(stack, port, fingerprint, 'Visitor', secrets)
            initial = read_board(alice, 2)
            assert initial['can_search'] and initial['searching'] is None
            assert initial['territories'] == {1: (1, 0, 0, 0, 0), 2: (0, 0, 0, 0, 0), 3: (0, 0, 0, 0, 0)}
            # The authenticated account must have one usable lobby session.
            # A duplicate login disables both sessions until deferred cleanup
            # removes the closed connection; availability is the wire barrier,
            # rather than assuming disconnect/prune occurs in the same tick.
            duplicate = log_in(stack, port, fingerprint, 'Alice', secrets)
            blocked = wait_board(alice, 100, lambda b: not b['can_search'])
            assert blocked['searching'] is None
            assert not read_board(duplicate, 101)['can_search']
            for peer, request_id in ((alice, 102), (duplicate, 103)):
                request(peer, 'CrusadesSearchBattle', request_id, territory=1)
                error(peer, request_id, 8)
            duplicate.close()
            restored = wait_board(alice, 104, lambda b: b['can_search'] and b['searching'] is None)
            assert restored['territories'][1][1:3] == (0, 0)
            fresh = search(alice, 105)
            assert fresh['searching'] == 1
            stable = wait_board(alice, 106, lambda b: b['can_search'] and b['searching'] == 1)
            assert stable['expires'] == fresh['expires']
            assert stable['territories'][1] == (1, 1, 0, 0, 0), 'old session cleanup erased a fresh search'
            assert cancel(alice, 107)['searching'] is None
            cleared = wait_board(alice, 108, lambda b: b['can_search'] and b['searching'] is None)
            assert cleared['territories'][1] == (1, 0, 0, 0, 0)
            assert not read_board(visitor, 3)['can_search']
            request(visitor, 'CrusadesSearchBattle', 4, territory=1)
            error(visitor, 4, 8)
            for identity, territory in ((5, 2), (6, 3), (7, 999)):
                request(alice, 'CrusadesSearchBattle', identity, territory=territory)
                error(alice, identity, 8)
            request(alice, 'CrusadesSearchBattle', 8, territory=1, extra=b'forged account')
            error(alice, 8, 1)
            request(alice, 'CrusadesGetMatchmaking', 9, version=WIRE_VERSION + 1)
            error(alice, 9, 2)
            request(alice, 'CrusadesGetMatchmaking', 10, campaign='missing')
            error(alice, 10, 5)
            first = search(alice, 11)
            assert first['searching'] == 1 and first['territories'][1] == (1, 1, 0, 0, 0)
            assert 0 < first['expires'] - int(time.time()) <= 600
            repeated = search(alice, 12)
            assert repeated['expires'] == first['expires'], 'repeated request renewed queue lifetime'
            assert repeated['territories'][1] == first['territories'][1], 'duplicate request doubled waiting account'
            same_side = search(carol, 13)
            assert same_side['searching'] == 1 and same_side['territories'][1] == (1, 2, 0, 0, 0)
            # Server-only aggregate/status packets never grant client authority.
            before = read_board(carol, 14)
            carol.send('CrusadesMatchmakingStatus', struct.pack('<HI', WIRE_VERSION, 0) + b'forged')
            carol.send('CrusadesCampaignSnapshot', struct.pack('<HI', WIRE_VERSION, 0) + b'forged')
            after = read_board(carol, 15)
            assert after['territories'] == before['territories']
            assert after['searching'] == before['searching']
            matched = search(bob, 16)
            assert matched['searching'] is None and not matched['can_search']
            identity, room, map_id, _ = battle.result(alice)
            assert battle.result(bob)[:3] == (identity, room, map_id)
            assert map_id == b'Frey River Plain'
            assert room > 0
            assert alice.receive('JoinResult').num('<B') == 1
            baseline = receive_lobby(alice, room)
            assert baseline[0] == room and baseline[2][0] == 1
            assert read_board(carol, 17)['territories'][1] == (1, 1, 0, 1, 0), 'FIFO did not leave newer same-side waiter'
            bob.send('CrusadesSearchBattle', struct.pack('<HI', WIRE_VERSION, 18) + auth.field('synthetic') + struct.pack('<I', 1))
            error(bob, 18, 8)
            battle.join(carol, room, False)
            battle.join(bob, room, True)
            assert receive_lobby(bob, room)[0] == room
            assert receive_lobby(alice, room)[0] == room
            receive_map_offer(alice, room); receive_map_offer(bob, room)
            # The automatically paired room keeps immutable authoritative rules.
            changed = bytearray(baseline[2]); changed[0] = 0
            alice.send('SetGameOptions', changed)
            alice.send('SlotUpdate', bytes([1, 2, 4, 9, 0, 1, 4]))
            # Rejected edits cannot affect the launch context stored with this
            # battle; the start path independently verifies it again below.
            alice.send('LeaveGame')
            auth.request(alice)
            assert battle.result(alice, status=4)[0] == identity
            assert battle.result(bob, status=4)[0] == identity
            assert cancel(carol, 19)['searching'] is None
            assert cancel(carol, 20)['territories'][1] == (1, 0, 0, 0, 0)
            with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
                assert db.execute('SELECT revision FROM campaigns').fetchone()[0] == 0
                assert db.execute('SELECT count(*) FROM verified_match_results').fetchone()[0] == 0
            # Searches are volatile and owned by the live authenticated session.
            queued = search(dave, 21)
            assert queued['searching'] == 1
            dave.close()
            wait_board(carol, 22, lambda b: b['territories'][1][2] == 0)
            reconnected = log_in(stack, port, fingerprint, 'Dave', secrets)
            assert read_board(reconnected, 23)['searching'] is None
            # A second pairing yields one durable capability, one room and at
            # most one authoritative result, using the real reference referee.
            bob.send('LeaveGame')
            auth.request(bob)
            search(alice, 24)
            search(bob, 25)
            completed_id, completed_room, _, _ = battle.result(alice)
            assert completed_id != identity and completed_room != room
            assert battle.result(bob)[0] == completed_id
            alice.receive('JoinResult')
            initial_room = receive_lobby(alice, completed_room)
            battle.join(bob, completed_room, True)
            active = receive_lobby(bob, completed_room)
            seated_host = receive_lobby(alice, completed_room)
            assert active[1:3] == initial_room[1:3] == seated_host[1:3]
            assert active[1] == map_id and active[2] == baseline[2]
            assert all(active[3][i][0][0] == 1 for i in (0, 1))
            assert active[3][0][0][3] != active[3][1][0][3]
            for slot, peer in enumerate((alice, bob)):
                map_ready(peer, completed_room)
                values = active[3][slot][0]
                peer.send('SlotUpdate', bytes([slot, 1, values[1], values[2], values[3], 1, values[5]]))
            while True:
                ready = receive_lobby(alice, completed_room)
                assert ready[1:3] == active[1:3]
                assert [ready[3][i][0][:4] for i in (0, 1)] == [active[3][i][0][:4] for i in (0, 1)]
                if ready[4] and all(ready[3][i][0][4] for i in (0, 1)):
                    break
            alice.send('StartGame')
            for peer in (alice, bob):
                starting = receive_lobby(peer, completed_room, 'GameStarting')
                assert starting[1:3] == active[1:3]
                peer.send('Loaded', struct.pack('<Q', fingerprint))
            assert read_board(carol, 26)['territories'][1] == (1, 0, 0, 0, 1)
            for peer in (alice, bob):
                assert peer.receive('TickBundle').num('<I') == 0
            command = struct.pack('<BBiiffB16s', 12, 0, 1, 0, 0, 0, 0, bytes(16))
            alice.send('PlayerCommands', struct.pack('<I', 1) + command)
            outcome, winner, replay_id, replay_digest = results.result(root, completed_id)
            assert outcome == 0 and winner == 'bob' and replay_id and replay_digest
            results.verify_replay(root, completed_id, replay_digest, fingerprint)
            bob.send('CrusadesBattleResult', bytes([0]) + auth.field('forged duplicate result'))
            auth.request(bob)
            assert results.result(root, completed_id) == (outcome, winner, replay_id, replay_digest)
            assert search(carol, 29)['searching'] == 1
            with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
                assert db.execute('SELECT count(*) FROM issued_battles').fetchone()[0] == 2
                assert db.execute('SELECT count(*) FROM battle_rooms').fetchone()[0] == 1
                assert db.execute('SELECT count(*) FROM verified_match_results').fetchone()[0] == 1
                assert db.execute('SELECT revision FROM campaigns').fetchone()[0] == 0
                assert db.execute('SELECT count(*) FROM campaign_events').fetchone()[0] == 1
        # Restart cannot restore abandoned volatile search entries or revive old
        # room IDs. Authenticated durable battle status remains queryable.
        with auth.server(server, data, root) as (port, fingerprint), contextlib.ExitStack() as stack:
            alice = log_in(stack, port, fingerprint, 'Alice', secrets)
            assert read_board(alice, 27)['searching'] is None
            request(alice, 'CrusadesGetBattleStatus', 28, campaign=completed_id)
            status = response(alice, 'CrusadesBattleStatus', 28)
            assert status.field() == b'synthetic' and status.field() == completed_id.encode()
            status.num('<Q'); assert status.num('<I') == 1 and status.num('<B') == 3
            status.field(); status.num('<Q'); assert status.num('<I') == 0
    print('PASS: real server matchmaking, FIFO alliances, eligibility, cancellation, privacy, disconnect/restart and referee result linkage')


def run_ui(server, client, data, root):
    if not sys.platform.startswith('linux'):
        raise RuntimeError('--client SDL smoke currently requires Linux isolated preferences')
    import crusades_ui_network_test as ui
    root.mkdir(parents=True, exist_ok=True)
    definition(root)
    secrets = []
    with auth.server(server, data, root) as (port, fingerprint), contextlib.ExitStack() as stack:
        alice = log_in(stack, port, fingerprint, 'Alice', secrets, 1)
        alice.close()
        bob = log_in(stack, port, fingerprint, 'Bob', secrets, 2)
        # Keep this actual UI live long enough to observe both transitions from
        # a separate account. Process teardown cannot masquerade as Cancel.
        clicks = ';'.join(['40,135', '320,497'] + ['600,420'] * 120 + ['500,497'] + ['600,420'] * 20)
        process, log, shot = ui.launch(client, data, root, port, 'find-cancel', clicks, 2500)
        saw_search = saw_cancel = False
        try:
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline and process.poll() is None:
                value = read_board(bob, 50)
                count = value['territories'][1][1]
                saw_search |= count == 1
                if saw_search and count == 0 and process.poll() is None:
                    saw_cancel = True
                    break
                time.sleep(.1)
            ui.finish(process, log, shot)
            assert saw_search and saw_cancel, 'actual SDL Find/Cancel did not change live server queue'
        finally:
            if process.poll() is None:
                process.kill(); process.wait()
            log.close()
        search(bob, 51)
        # The oldest waiter is host; actual UI Alice gets the invitation. Join
        # must work without manually selecting a history card first.
        clicks = ';'.join(['40,135', '320,497'] + ['600,420'] * 30 + ['700,478'] + ['600,420'] * 10)
        process, log, shot = ui.launch(client, data, root, port, 'matched-guest-join', clicks, 2500)
        try:
            identity, room, _, _ = battle.result(bob)
            ui.finish(process, log, shot)
            text = (root / 'server.log').read_text(errors='replace')
            import re
            assert re.search(r'client \d+ joined game ' + str(room) + r' at slot 1', text), 'actual matched guest did not join invitation'
            with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
                assert db.execute('SELECT count(*) FROM issued_battles WHERE id=?', (identity,)).fetchone()[0] == 1
                assert db.execute('SELECT revision FROM campaigns').fetchone()[0] == 0
        finally:
            if process.poll() is None:
                process.kill(); process.wait()
            log.close()
    print('PASS: actual GameView SDL Find/Cancel, opponent pairing and selected invitation Join')
    print('Artifacts:', root)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--client', type=Path, help='optional Debug takclient for actual SDL smoke')
    parser.add_argument('--artifacts', type=Path)
    args = parser.parse_args()
    run(args.server.resolve(), args.data.resolve())
    if args.client:
        if args.artifacts:
            run_ui(args.server.resolve(), args.client.resolve(), args.data.resolve(), args.artifacts.resolve())
        else:
            with tempfile.TemporaryDirectory(prefix='tak-crusades-match-ui-') as temporary:
                run_ui(args.server.resolve(), args.client.resolve(), args.data.resolve(), Path(temporary))
