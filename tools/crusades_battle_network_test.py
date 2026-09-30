#!/usr/bin/env python3
"""Real-server issuance/room-isolation tests using synthetic campaign accounts."""
import argparse
from collections import defaultdict, deque
import contextlib
from pathlib import Path
import re
import sqlite3
import struct
import tempfile

import crusades_auth_network_test as auth

MAX_SLOTS = int(re.search(r'kMaxSlots = (\d+)', auth.header)[1])


class Peer(auth.Peer):
    def __init__(self, port):
        super().__init__(port)
        self.pending = defaultdict(deque)

    def receive(self, kind):
        wanted = auth.MSG[kind]
        if self.pending[wanted]:
            return auth.Reader(self.pending[wanted].popleft())
        while True:
            size = struct.unpack('<I', self.exact(4))[0]
            assert 1 <= size <= 16 * 1024 * 1024
            message = self.exact(size)
            if message[0] == auth.MSG['Ping']:
                self.send('Pong')
            elif message[0] == wanted:
                return auth.Reader(message[1:])
            else:
                self.pending[message[0]].append(message[1:])


def result(peer, status=0):
    r = peer.receive('CrusadesBattleResult')
    actual = r.num('<B')
    campaign, battle = r.field(), r.field()
    room, map_id, expires = r.num('<I'), r.field(), r.num('<Q')
    explanation = r.field()
    assert r.pos == len(r.data)
    assert actual == status, (actual, status, explanation)
    return battle.decode(), room, map_id, expires


def issue(peer, opponent='Bob', territory=1, status=0):
    peer.send('CrusadesIssueBattle', auth.field('synthetic') + struct.pack('<I', territory) + auth.field(opponent))
    return result(peer, status)


def lobby(r):
    room = r.num('<I')
    name, map_id, mission = r.field(), r.field(), r.field()
    options = r.data[r.pos:r.pos + 15]
    r.pos += 15
    host, ready = r.num('<I'), r.num('<B')
    slots = []
    for _ in range(MAX_SLOTS):
        values = tuple(r.num('<B') for _ in range(6))
        slots.append((values, r.field()))
    return room, map_id, options, slots, ready


def map_ready(peer, room):
    r = peer.receive('MapOffer')
    assert r.num('<I') == room
    r.field()
    digest = r.field()
    assert r.num('<I') > 0
    peer.send('MapReady', struct.pack('<I', room) + auth.field(digest))


def join(peer, room, success):
    peer.send('JoinGame', struct.pack('<I', room) + auth.field(''))
    reply = peer.receive('JoinResult')
    assert bool(reply.num('<B')) == success
    return reply.num('<B')


def run(binary, data):
    with tempfile.TemporaryDirectory(prefix='tak-issued-battle-') as temporary:
        root = Path(temporary)
        (root / 'synthetic.campaign').write_text(
            'campaign 1 "synthetic" "Synthetic battle test"\n'
            'territory 1 "One"\nmap 1 "Frey River Plain"\nterritory 2 "Unknown map"\n')
        secrets = []
        with auth.server(binary, data, root) as (port, fingerprint), contextlib.ExitStack() as stack:
            alice, bob, other = [stack.enter_context(contextlib.closing(Peer(port))) for _ in range(3)]
            issue(alice, status=2)
            for peer, user, side in [(alice, 'Alice', 1), (bob, 'Bob', 2), (other, 'Other', 1)]:
                peer.hello(fingerprint)
                peer.login(user, secrets)
                auth.request(peer, 2**64 - 1, side)
            issue(alice, opponent='Nobody', status=4)
            issue(alice, opponent='Other', status=4)
            issue(alice, territory=2, status=4)
            issue(alice, territory=99, status=4)
            abandoned, first_room, _, _ = issue(alice)
            assert result(bob)[0] == abandoned
            alice.receive('JoinResult')
            alice.receive('LobbyState')
            alice.receive('MapOffer')
            alice.send('LeaveGame')
            # An allegiance query is a wire-order barrier after leaving.
            auth.request(alice)
            result(alice, status=4)
            battle, room, map_id, _ = issue(alice)
            assert battle != abandoned and room != first_room
            assert result(bob)[0] == battle
            alice.receive('JoinResult')
            baseline = lobby(alice.receive('LobbyState'))
            assert baseline[0] == room and baseline[2][0] == 1
            join(other, room, False)
            join(bob, room, True)
            # Host-supplied changes cannot replace issued rules/map/participants.
            changed_options = bytearray(baseline[2]); changed_options[0] = 0
            alice.send('SetGameOptions', bytes(changed_options))
            alice.send('SlotUpdate', bytes([1, 2, 4, 9, 0, 1, 4]))
            alice.send('MapOffer', struct.pack('<I', room) + auth.field('wrong map') + auth.field('0' * 64) + struct.pack('<I', 1))
            map_ready(alice, room)
            map_ready(bob, room)
            for peer, slot in [(alice, 0), (bob, 1)]:
                values = baseline[3][slot][0]
                peer.send('SlotUpdate', bytes([slot, 1, values[1], values[2], values[3], 1, values[5]]))
            while True:
                ready = lobby(alice.receive('LobbyState'))
                if ready[3][0][0][4] and ready[3][1][0][4] and ready[4]:
                    break
            assert ready[1] == map_id and ready[2] == baseline[2]
            assert [ready[3][i][0][0] for i in (0, 1)] == [1, 1]
            assert ready[3][0][0][3] != ready[3][1][0][3]
            alice.send('StartGame')
            starting = alice.receive('GameStarting')
            assert lobby(starting)[2] == baseline[2]
            starting.num('<B'); starting.num('<I')
            stolen_token = starting.num('<Q')
            bob.receive('GameStarting')
            # A token from another player's slot must not replace authentication.
            alice.close()
            dropped = bob.receive('PlayerStatus')
            assert (dropped.num('<B'), dropped.num('<B')) == (0, 1)
            other.send('Rejoin', struct.pack('<IQ', room, stolen_token))
            denied = other.receive('JoinResult')
            assert denied.num('<B') == 0
            denied.num('<B')
            assert denied.field() == b'resume account does not match campaign participant'
            resumed = stack.enter_context(contextlib.closing(Peer(port)))
            resumed.hello(fingerprint)
            resumed.login('Alice', secrets)
            resumed.send('Rejoin', struct.pack('<IQ', room, stolen_token))
            restored = resumed.receive('GameStarting')
            assert lobby(restored)[0] == room
            assert restored.num('<B') == 0
            restored.num('<I')
            assert restored.num('<Q') != stolen_token
            resumed.send('LeaveGame')
            auth.request(resumed)
            bob.send('LeaveGame')
            auth.request(bob)
            # Ordinary room creation cannot manufacture an issued battle binding.
            ordinary_options = bytes([0, 0, 0, 10, 0]) + struct.pack('<I', 2000) + bytes([0, 0, 1, 0, 0, 0])
            other.send('CreateGame', auth.field('ordinary') + auth.field('') + auth.field('Frey River Plain') + auth.field('') + ordinary_options + bytes([2, 0, 0]))
            assert other.receive('JoinResult').num('<B') == 1
            other.send('LeaveGame')
            auth.request(other)
        with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
            assert db.execute('SELECT revision FROM campaigns').fetchone()[0] == 0
            assert db.execute('SELECT count(*) FROM campaign_events').fetchone()[0] == 1
            assert db.execute('SELECT count(*) FROM battle_results').fetchone()[0] == 0
            assert db.execute('SELECT count(*) FROM issued_battles').fetchone()[0] == 2
            assert db.execute('SELECT count(*) FROM battle_rooms').fetchone()[0] == 1
            assert db.execute('SELECT status FROM battle_status_events ORDER BY battle_id,revision').fetchall().count((1,)) == 1
            assert db.execute('SELECT count(*) FROM issued_battles b JOIN battle_status_events e '
                              'ON e.battle_id=b.id AND e.revision=b.revision WHERE e.status=2').fetchone()[0] == 2
        with auth.server(binary, data, root) as (port, fingerprint), contextlib.closing(Peer(port)) as reconnect:
            reconnect.hello(fingerprint)
            reconnect.login('Alice', secrets)
            join(reconnect, room, False)
    print('PASS: real server-issued rooms, authenticated roster, immutable rules, start and normal-room isolation')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    args = parser.parse_args()
    run(args.server.resolve(), args.data.resolve())
