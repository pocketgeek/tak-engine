#!/usr/bin/env python3
"""Completed territory archives and retained replay pulls over real SCRAM sockets."""
import argparse
import contextlib
import hashlib
from pathlib import Path
import re
import sqlite3
import struct
import tempfile
import time

import crusades_auth_network_test as auth
import crusades_battle_network_test as battle
import crusades_result_network_test as results

PAYLOAD_VERSION = int(re.search(r'kVersion = (\d+)',
    (auth.ROOT / 'src/net/crusades.h').read_text())[1])
CHUNK_BYTES = 64 * 1024
ABSENT = 2**64 - 1


class ArchivePeer(battle.Peer):
    def __init__(self, port):
        super().__init__(port)
        self.next_id = 1
        self.archive_wire = []

    def query(self, kind, extra=b'', version=PAYLOAD_VERSION):
        identity = self.next_id
        self.next_id += 1
        self.send(kind, struct.pack('<HI', version, identity) + extra)
        return identity

    def answer(self, kind, identity):
        while True:
            reader = self.receive(kind)
            assert reader.num('<H') == PAYLOAD_VERSION
            actual = reader.num('<I')
            self.archive_wire.append(reader.data)
            if actual == identity:
                return reader
            assert actual == 0, 'response correlation mismatch'

    def error(self, identity, code):
        reader = self.answer('CrusadesError', identity)
        assert reader.num('<B') == code
        assert reader.field() == b''
        assert reader.num('<B') == 0
        assert reader.field()
        assert reader.pos == len(reader.data)


def flag(reader):
    value = reader.num('<B')
    assert value in (0, 1)
    return bool(value)


def history_extra(territory=1, cursor=None, limit=16):
    value = auth.field('synthetic') + struct.pack('<IB', territory, cursor is not None)
    if cursor:
        value += struct.pack('<Q', cursor[0]) + auth.field(cursor[1])
    return value + struct.pack('<H', limit)


def history(peer, territory=1, cursor=None, limit=16):
    identity = peer.query('CrusadesGetTerritoryHistory', history_extra(territory, cursor, limit))
    reader = peer.answer('CrusadesTerritoryHistory', identity)
    assert reader.field() == b'synthetic'
    assert reader.num('<I') == territory
    count = reader.num('<H')
    assert count <= limit <= 32
    entries = []
    for _ in range(count):
        entry = {'id': reader.field().decode(), 'territory': reader.num('<I'),
                 'revision': reader.num('<Q'), 'recorded': reader.num('<Q'),
                 'map': reader.field().decode(), 'outcome': reader.num('<B'),
                 'tick': reader.num('<Q'), 'hash': reader.num('<Q')}
        entry['winners'] = [reader.field().decode() for _ in range(reader.num('<B'))]
        participants = []
        for _ in range(reader.num('<B')):
            person = {'account': reader.field().decode()}
            for name, fmt in [('kills', '<Q'), ('losses', '<Q'), ('score', '<q'),
                              ('built', '<Q'), ('units', '<Q')]:
                person[name] = reader.num(fmt)
            person.update(faction=reader.field().decode(), team=reader.num('<B'), defeated=flag(reader))
            assert person['faction'] and person['team'] <= 7
            participants.append(person)
        entry['participants'] = participants
        entry['replay'] = None
        if flag(reader):
            entry['replay'] = {'digest': reader.field().decode(), 'size': reader.num('<Q'),
                              'format': reader.num('<I'), 'protocol': reader.num('<I'),
                              'map_digest': reader.field().decode(), 'fingerprint': reader.num('<Q')}
            assert re.fullmatch('[0-9a-f]{64}', entry['replay']['digest'])
            assert 0 < entry['replay']['size'] <= 512 * 1024 * 1024
            assert entry['replay']['format'] == 12
            assert entry['replay']['protocol'] == auth.VERSION
        assert entry['territory'] == territory and entry['recorded'] > 0
        entries.append(entry)
    next_cursor = (reader.num('<Q'), reader.field().decode()) if flag(reader) else None
    assert reader.pos == len(reader.data)
    keys = [(entry['recorded'], entry['id']) for entry in entries]
    assert keys == sorted(keys, reverse=True) and len(keys) == len(set(keys))
    if cursor:
        assert all(key < cursor for key in keys)
    if next_cursor:
        assert next_cursor == keys[-1]
    return entries, next_cursor


def replay_extra(identity, offset=0, limit=CHUNK_BYTES):
    return auth.field(identity) + struct.pack('<QI', offset, limit)


def chunk(peer, entry, offset=0, limit=CHUNK_BYTES):
    identity = peer.query('CrusadesGetReplayChunk', replay_extra(entry['id'], offset, limit))
    reader = peer.answer('CrusadesReplayChunk', identity)
    assert reader.field().decode() == entry['id']
    assert reader.field().decode() == entry['replay']['digest']
    total, actual_offset = reader.num('<Q'), reader.num('<Q')
    final = flag(reader)
    count = reader.num('<I')
    assert total == entry['replay']['size'] and actual_offset == offset
    assert 0 < count <= limit and count <= total - offset
    assert final == (offset + count == total)
    payload = reader.data[reader.pos:]
    assert len(payload) == count
    return payload, final


def download(peer, entry, destination):
    # Pull sequential bounded chunks into a file; never collect a large replay in RAM.
    digest = hashlib.sha256()
    offset = 0
    with destination.open('wb') as output:
        while offset < entry['replay']['size']:
            payload, final = chunk(peer, entry, offset, limit=128)
            output.write(payload)
            digest.update(payload)
            offset += len(payload)
            assert final == (offset == entry['replay']['size'])
            time.sleep(.025)  # Keep requests below the separate replay quota.
    assert destination.stat().st_size == offset
    return digest.hexdigest()


def enrolled_peer(port, fingerprint, user, stack, secrets):
    peer = stack.enter_context(contextlib.closing(ArchivePeer(port)))
    peer.hello(fingerprint)
    peer.login(user, secrets)
    return peer


def start_existing(alice, bob):
    identity, room, _, _ = battle.issue(alice)
    assert battle.result(bob)[0] == identity
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
        peer.receive('GameStarting')
    return identity


def finish(root, identity, alice, bob, fingerprint):
    for peer in (alice, bob):
        peer.send('Loaded', struct.pack('<Q', fingerprint))
    for peer in (alice, bob):
        assert peer.receive('TickBundle').num('<I') == 0
    alice.send('LeaveGame')
    recorded = results.result(root, identity)
    assert recorded[0] == 1 and recorded[1] == 'bob' and recorded[2] and recorded[3]
    bob.send('LeaveGame')
    auth.request(bob)  # Server wire-order barrier for leaving its finished room.
    return recorded


def database_state(root):
    with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
        return tuple(tuple(db.execute(query)) for query in (
            'SELECT * FROM verified_match_results ORDER BY battle_id',
            'SELECT * FROM territory_battle_history ORDER BY battle_id',
            'SELECT * FROM campaign_events ORDER BY campaign_id,revision',
            'SELECT * FROM rule_decisions ORDER BY battle_id',
            'SELECT revision FROM campaigns ORDER BY id'))


def audit_wire(root, peers, secrets):
    with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
        private = [str(root).encode(), b'crusades-replays', b'.takrep']
        private += [row[0].encode() for row in db.execute('SELECT launch_token FROM issued_battles')]
        private += [row[0].encode() for row in db.execute('SELECT room_token FROM battle_rooms')]
        private += [row[0].encode() for row in db.execute('SELECT replay_id FROM verified_match_results WHERE replay_id IS NOT NULL')]
    for peer in peers:
        for wire in peer.archive_wire:
            assert all(value not in wire for value in private + secrets if value), 'archive exposed a private value'


def run(binary, data):
    with tempfile.TemporaryDirectory(prefix='tak-history-network-') as temporary:
        root = Path(temporary)
        (root / 'synthetic.campaign').write_text(
            'campaign 1 "synthetic" "Synthetic history test"\n'
            'territory 1 "One"\nmap 1 "Frey River Plain"\nterritory 2 "No battles"\n')
        secrets = []
        with auth.server(binary, data, root) as (port, fingerprint), contextlib.ExitStack() as stack:
            anonymous = stack.enter_context(contextlib.closing(ArchivePeer(port)))
            anonymous.error(anonymous.query('CrusadesGetTerritoryHistory', history_extra()), 3)
            anonymous.error(anonymous.query('CrusadesGetReplayChunk', replay_extra('unknown')), 3)
            alice = enrolled_peer(port, fingerprint, 'Alice', stack, secrets)
            bob = enrolled_peer(port, fingerprint, 'Bob', stack, secrets)
            other = enrolled_peer(port, fingerprint, 'Other', stack, secrets)
            outsider = enrolled_peer(port, fingerprint, 'Outsider', stack, secrets)
            for peer, alliance in ((alice, 1), (bob, 2), (other, 1)):
                auth.request(peer, ABSENT, alliance)
            outsider.error(outsider.query('CrusadesGetTerritoryHistory', history_extra()), 9)
            identity = start_existing(alice, bob)
            # The completed archive does not grant visibility into active private rooms.
            other.error(other.query('CrusadesGetBattleStatus', auth.field(identity)), 5)
            assert history(other)[0] == []
            other.error(other.query('CrusadesGetReplayChunk', replay_extra(identity)), 5)
            saved = finish(root, identity, alice, bob, fingerprint)
            entries, cursor = history(alice)
            assert len(entries) == 1 and cursor is None
            entry = entries[0]
            assert entry['id'] == identity and entry['map'] == 'Frey River Plain'
            assert entry['outcome'] == 1 and entry['winners'] == ['bob'] and entry['tick'] > 0
            assert {p['account'] for p in entry['participants']} == {'alice', 'bob'}
            assert entry['replay']['digest'] == saved[3] and entry['replay']['fingerprint'] == fingerprint
            assert history(bob)[0] == entries and history(other)[0] == entries
            other.error(other.query('CrusadesGetBattleStatus', auth.field(identity)), 5)
            outsider.error(outsider.query('CrusadesGetReplayChunk', replay_extra(identity)), 9)
            outsider.error(outsider.query('CrusadesGetReplayChunk', replay_extra('unknown')), 5)
            assert history(other, territory=2) == ([], None)
            assert history(other, cursor=(entry['recorded'], identity)) == ([], None)
            for extra in (history_extra(limit=0), history_extra(limit=33), history_extra()+b'x'):
                other.error(other.query('CrusadesGetTerritoryHistory', extra), 1)
            other.error(other.query('CrusadesGetTerritoryHistory', history_extra(), version=PAYLOAD_VERSION+1), 2)
            for extra in (replay_extra(identity, limit=0), replay_extra(identity, limit=CHUNK_BYTES+1),
                          replay_extra(identity)+b'x'):
                other.error(other.query('CrusadesGetReplayChunk', extra), 1)
            other.error(other.query('CrusadesGetReplayChunk', replay_extra('../outside')), 5)
            other.error(other.query('CrusadesGetReplayChunk', replay_extra(identity, entry['replay']['size'])), 8)
            assert download(other, entry, root/'download.bin') == saved[3]
            files = [p for p in root.rglob('*.takrep') if hashlib.sha256(p.read_bytes()).hexdigest() == saved[3]]
            assert len(files) == 1
            artifact = files[0]
            original = artifact.read_bytes()
            # Forty pipelined pulls in one window exceed the normal 32-query
            # quota while remaining below the replay service's separate limit.
            time.sleep(1.05)
            burst = [other.query('CrusadesGetReplayChunk', replay_extra(identity, limit=1)) for _ in range(40)]
            for request_id in burst:
                reader = other.answer('CrusadesReplayChunk', request_id)
                assert reader.field().decode() == identity
                assert reader.field().decode() == saved[3]
                assert reader.num('<Q') == len(original) and reader.num('<Q') == 0
                assert not flag(reader) and reader.num('<I') == 1
                assert reader.data[reader.pos:] == original[:1]
            time.sleep(1.05)
            baseline = database_state(root)
            stable = dict(entry, replay=None)
            # Missing retained bytes leave the completed result visible and durable.
            artifact.unlink()
            assert history(other)[0] == [stable]
            other.error(other.query('CrusadesGetReplayChunk', replay_extra(identity)), 8)
            assert database_state(root) == baseline
            # A malformed header similarly suppresses availability without a DB write.
            artifact.write_bytes(b'BAD!' + original[4:])
            assert history(other)[0] == [stable]
            other.error(other.query('CrusadesGetReplayChunk', replay_extra(identity)), 8)
            assert database_state(root) == baseline
            # Serving uses a regular retained file handle, never a symlink target.
            reference = root / 'outside-retention.bin'
            reference.write_bytes(original)
            artifact.unlink()
            try:
                artifact.symlink_to(reference)
            except OSError:
                pass  # Some Windows accounts lack permission to create symlinks.
            else:
                assert history(other)[0] == [stable]
                other.error(other.query('CrusadesGetReplayChunk', replay_extra(identity)), 8)
                assert database_state(root) == baseline
                artifact.unlink()
            # Body corruption passes the cheap header check; the transferred SHA fails.
            artifact.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
            corrupt, _ = history(other)
            assert corrupt == entries
            assert download(other, corrupt[0], root/'corrupt.bin') != saved[3]
            assert database_state(root) == baseline
            artifact.write_bytes(original)
            assert history(other)[0] == entries
            audit_wire(root, (anonymous, alice, bob, other, outsider), secrets)
        # A real process restart retains records and retained artifacts, then a
        # second referee completion gives pagination a genuine older record.
        with auth.server(binary, data, root) as (port, fingerprint), contextlib.ExitStack() as stack:
            alice = enrolled_peer(port, fingerprint, 'ALICE', stack, secrets)
            bob = enrolled_peer(port, fingerprint, 'BOB', stack, secrets)
            other = enrolled_peer(port, fingerprint, 'OTHER', stack, secrets)
            assert history(other)[0] == entries
            assert download(other, entry, root/'restart.bin') == saved[3]
            second = start_existing(alice, bob)
            assert second != identity
            finish(root, second, alice, bob, fingerprint)
            both, cursor = history(other)
            assert len(both) == 2 and {b['id'] for b in both} == {identity, second} and cursor is None
            first_page, cursor = history(other, limit=1)
            assert first_page == both[:1] and cursor is not None
            last_page, final_cursor = history(other, cursor=cursor, limit=1)
            assert last_page == both[1:] and final_cursor is None
            assert history(other, cursor=(last_page[0]['recorded'],last_page[0]['id']),limit=1) == ([],None)
            audit_wire(root, (alice, bob, other), secrets)
    print('PASS: real referee history, enrolled archive access, private room isolation, exact replay SHA, retention/corruption invariance, keyset pagination and restart')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    args = parser.parse_args()
    run(args.server.resolve(), args.data.resolve())
