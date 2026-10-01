#!/usr/bin/env python3
"""Public-service boundaries against a real server, using synthetic accounts/state."""
import argparse
from collections import defaultdict, deque
import contextlib
import os
from pathlib import Path
import re
import socket
import shutil
import sqlite3
import struct
import subprocess
import tempfile
import time

import crusades_auth_network_test as auth
import crusades_battle_network_test as battle
import crusades_protocol_network_test as protocol
import crusades_result_network_test as results


def frame(kind, payload=b''):
    return struct.pack('<IB', len(payload) + 1, auth.MSG[kind]) + payload


class Peer(battle.Peer):
    def __init__(self, port, source=None):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=10,
            source_address=(source, 0) if source else None)
        self.pending = defaultdict(deque)
        self.sent_proof = None

    def send(self, kind, payload=b''):
        if kind == 'AuthProof':
            self.sent_proof = payload
        super().send(kind, payload)

    def drain_barrier(self):
        self.send('Ping')
        self.receive('Pong')
        return self.pending


@contextlib.contextmanager
def server(binary, data, root):
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    command = [str(binary), '--local', '--data', str(data), '--port', str(port),
               '--accounts', str(root / 'accounts.conf'), '--crusades-db', str(root / 'campaign.sqlite'),
               '--crusades-definition', str(root / 'synthetic.campaign')]
    with (root / 'server.log').open('w') as log:
        process = subprocess.Popen(command, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                text = (root / 'server.log').read_text(errors='replace')
                if 'listening on' in text:
                    fingerprint = int(re.search(r'retail gameplay hash ([0-9a-f]+)', text)[1], 16)
                    yield port, fingerprint, process
                    return
                if process.poll() is not None:
                    raise AssertionError('startup failed: ' + text)
                time.sleep(.05)
            raise AssertionError('startup timed out')
        finally:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def prepare(root):
    (root / 'synthetic.campaign').write_text(
        'campaign 1 "synthetic" "Synthetic hardening test"\n'
        'territory 1 "One"\nmap 1 "Frey River Plain"\n')


def login(port, fingerprint, user='Alice'):
    peer = Peer(port)
    peer.hello(fingerprint)
    peer.login(user, [])
    return peer


def closed(peer):
    peer.socket.settimeout(3)
    try:
        assert peer.socket.recv(1) == b'', 'connection still accepted'
    except ConnectionResetError:
        pass


def boundaries(binary, data, root):
    with server(binary, data, root) as (port, fingerprint, process):
        with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
            assert db.execute('SELECT action,actor,reason,expected_revision,before_revision,after_revision '
                'FROM admin_events').fetchall() == [
                    ('start', 'takserver', 'server configured campaign definition', -1, -1, 0)]
        for version, extra in [(auth.VERSION + 1, b''), (auth.VERSION, b'x')]:
            with contextlib.closing(Peer(port)) as peer:
                peer.send('Hello', struct.pack('<I', version) + auth.field('hardening') +
                          struct.pack('<Q', fingerprint) + auth.field('Alice') + extra)
                reason = peer.receive('Reject').field()
                assert b'version mismatch' in reason if not extra else b'malformed hello' in reason
                closed(peer)
        for nonce, extra in [(os.urandom(31), b''), (os.urandom(32), b'x')]:
            with contextlib.closing(Peer(port)) as peer:
                peer.hello(fingerprint)
                peer.send('AuthBegin', auth.field('Alice') + auth.field(nonce) + extra)
                assert b'malformed login' in peer.receive('Reject').field()
                closed(peer)
        with contextlib.closing(login(port, fingerprint)) as alice:
            auth.request(alice, protocol.ABSENT, 1)
        with contextlib.closing(login(port, fingerprint)) as alice:
            proof = alice.sent_proof
            assert proof
        with contextlib.closing(Peer(port)) as replay:
            replay.hello(fingerprint)
            replay.send('AuthBegin', auth.field('Alice') + auth.field(os.urandom(32)))
            replay.receive('AuthChallenge')
            replay.send('AuthProof', proof)
            assert replay.receive('AuthResult').num('<B') == 2
            auth.request(replay, status=2)
        with contextlib.closing(Peer(port)) as malformed:
            malformed.hello(fingerprint)
            malformed.send('AuthBegin', auth.field('Alice') + auth.field(os.urandom(32)))
            malformed.receive('AuthChallenge')
            malformed.send('AuthProof', proof + b'x')
            assert b'malformed login proof' in malformed.receive('Reject').field()
            closed(malformed)
        with contextlib.closing(Peer(port)) as malformed:
            malformed.hello(fingerprint)
            malformed.send('AuthBegin', auth.field('NeverCreated') + auth.field(os.urandom(32)))
            malformed.receive('AuthChallenge')
            malformed.send('AuthRegister', auth.field(bytes(32)) + auth.field(bytes(32)) + b'x')
            assert b'malformed registration' in malformed.receive('Reject').field()
            closed(malformed)

        # Mutating legacy messages used to bypass every campaign query quota.
        time.sleep(1.05)
        with contextlib.closing(login(port, fingerprint)) as alice:
            mutation = b''.join(frame('CrusadesSetAllegiance', auth.field('synthetic') +
                struct.pack('<QB', revision, 2 if revision % 2 == 0 else 1)) for revision in range(120))
            started = time.monotonic()
            alice.socket.sendall(mutation)
            pending = alice.drain_barrier()
            replies = [auth.Reader(data) for data in pending[auth.MSG['CrusadesAllegianceResult']]]
            statuses = []
            for reply in replies:
                assert reply.num('<B') == 1
                statuses.append(reply.num('<B'))
            assert len(statuses) < 120 and 4 in statuses, statuses
            assert statuses.count(0) <= 64
            assert time.monotonic() - started < 1, 'quota regression requires a one-second burst'
            # New socket and alternate case share the depleted account budget.
            with contextlib.closing(login(port, fingerprint, 'ALICE')) as churn:
                churn.send('CrusadesIssueBattle', auth.field('synthetic') + struct.pack('<I', 1) + auth.field('Bob'))
                reply = churn.receive('CrusadesBattleResult')
                assert reply.num('<B') == 4
                reply.field(); reply.field(); reply.num('<I'); reply.field(); reply.num('<Q')
                assert b'rate exceeded' in reply.field()
            with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
                assert db.execute('SELECT count(*) FROM allegiance_events').fetchone()[0] <= 65
                assert db.execute('SELECT count(*) FROM issued_battles').fetchone()[0] == 0
                assert db.execute('SELECT revision FROM campaigns').fetchone()[0] == 0

        # A maximum catalog page costs eight units rather than loading 64 full
        # campaigns for the cost of one inexpensive allegiance lookup.
        time.sleep(1.05)
        with contextlib.closing(login(port, fingerprint)) as alice:
            alice.socket.sendall(b''.join(frame('CrusadesListCampaigns', struct.pack('<HI', 4, n) +
                auth.field('') + struct.pack('<H', 64)) for n in range(1, 17)))
            pending = alice.drain_barrier()
            assert len(pending[auth.MSG['CrusadesCampaignList']]) == 8
            assert len(pending[auth.MSG['CrusadesError']]) == 1

        # IP quota covers anonymous work spread over separate sockets too.
        time.sleep(1.05)
        with contextlib.ExitStack() as stack:
            peers = [stack.enter_context(contextlib.closing(Peer(port))) for _ in range(5)]
            for peer in peers[:4]:
                peer.socket.sendall(frame('CrusadesGetAllegiance', auth.field('synthetic')) * 64)
                assert len(peer.drain_barrier()[auth.MSG['CrusadesAllegianceResult']]) == 64
            peers[4].send('CrusadesGetAllegiance', auth.field('synthetic'))
            reply = peers[4].receive('CrusadesAllegianceResult')
            assert (reply.num('<B'), reply.num('<B')) == (0, 4)

        # Independently addressed peers share a global work ceiling as well.
        time.sleep(1.05)
        with contextlib.ExitStack() as stack:
            peers = [stack.enter_context(contextlib.closing(Peer(port, '127.0.0.' + str(n)))) for n in range(1, 18)]
            for peer in peers[:16]:
                peer.socket.sendall(frame('CrusadesGetAllegiance', auth.field('synthetic')) * 64)
                assert len(peer.drain_barrier()[auth.MSG['CrusadesAllegianceResult']]) == 64
            peers[-1].send('CrusadesGetAllegiance', auth.field('synthetic'))
            reply = peers[-1].receive('CrusadesAllegianceResult')
            assert (reply.num('<B'), reply.num('<B')) == (0, 4)

        # Invalid frame lengths and a burst of tiny frames cannot monopolize
        # the event loop; a valid peer continues to answer afterward.
        with contextlib.closing(Peer(port)) as oversized:
            oversized.socket.sendall(struct.pack('<I', 0xffffffff))
            closed(oversized)
        with contextlib.closing(Peer(port)) as flood:
            flood.socket.sendall(frame('Pong') * 2000)
            closed(flood)
        with contextlib.closing(login(port, fingerprint)) as healthy:
            healthy.send('Ping')
            healthy.receive('Pong')
        assert process.poll() is None

        # Admission limits are enforced before allocating another Client.
        time.sleep(.1)
        with contextlib.ExitStack() as stack:
            sockets = [stack.enter_context(contextlib.closing(Peer(port))) for _ in range(72)]
            accepted, rejected = [], 0
            for peer in sockets:
                try:
                    peer.send('Ping')
                    peer.receive('Pong')
                    accepted.append(peer)
                except (RuntimeError, ConnectionResetError, BrokenPipeError):
                    rejected += 1
            assert len(accepted) <= 64 and rejected >= 8, (len(accepted), rejected)
            # Keep these unauthenticated sockets busy: pings must not extend
            # their absolute login deadline indefinitely.
            deadline = time.monotonic() + 32
            while accepted and time.monotonic() < deadline:
                live = []
                for peer in accepted:
                    try:
                        peer.send('Ping')
                        peer.receive('Pong')
                        live.append(peer)
                    except (RuntimeError, ConnectionResetError, BrokenPipeError):
                        pass
                accepted = live
                if accepted:
                    time.sleep(.5)
            assert not accepted, 'pings kept pending logins alive past their deadline'
        with contextlib.closing(login(port, fingerprint)) as healthy:
            healthy.send('Ping')
            healthy.receive('Pong')


def rejoin(peer, room, token, expected):
    peer.send('Rejoin', struct.pack('<IQ', room, token))
    reply = peer.receive('JoinResult')
    assert reply.num('<B') == 0
    reply.num('<B')
    assert expected in reply.field()


def starting(reader):
    room = battle.lobby(reader)[0]
    reader.num('<B'); reader.num('<I')
    return room, reader.num('<Q')


def begin(port, fingerprint, stack):
    peers = [stack.enter_context(contextlib.closing(login(port, fingerprint, user))) for user in ('Alice', 'Bob')]
    for peer, side in zip(peers, (1, 2)):
        auth.request(peer, protocol.ABSENT, side)
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
    tokens = []
    for peer in peers:
        actual_room, token = starting(peer.receive('GameStarting'))
        assert actual_room == room and token
        tokens.append(token)
        peer.send('Loaded', struct.pack('<Q', fingerprint))
    for peer in peers:
        peer.receive('TickBundle')
    return identity, room, peers, tokens


def reconnect(binary, data, root):
    with server(binary, data, root) as (port, fingerprint, _), contextlib.ExitStack() as stack:
        identity, room, (alice, bob), tokens = begin(port, fingerprint, stack)
        other = stack.enter_context(contextlib.closing(login(port, fingerprint, 'Other')))
        alice.close()
        while True:
            dropped = bob.receive('PlayerStatus')
            if (dropped.num('<B'), dropped.num('<B')) == (0, 1):
                break
        rejoin(other, room, tokens[0], b'account does not match')
        resumed = stack.enter_context(contextlib.closing(login(port, fingerprint, 'ALICE')))
        resumed.send('Rejoin', struct.pack('<IQ', room, tokens[0]))
        restored_room, rotated = starting(resumed.receive('GameStarting'))
        assert restored_room == room and rotated != tokens[0]
        owner = stack.enter_context(contextlib.closing(login(port, fingerprint)))
        rejoin(owner, room, tokens[0], b'invalid or expired resume token')
        # A legitimate final resignation freezes the referee's outcome for its
        # hash-report grace. A dropped winner cannot resume that frozen battle.
        bob.send('LeaveGame')
        bob.drain_barrier()
        time.sleep(.1)
        resumed.close()
        time.sleep(.05)
        rejoin(owner, room, rotated, b'campaign battle has ended')
        recorded = results.result(root, identity)
        assert recorded[0] == 1 and recorded[1] == 'alice', recorded


def interrupted_battle(binary, data, root):
    with server(binary, data, root) as (port, fingerprint, process), contextlib.ExitStack() as stack:
        identity, old_room, peers, tokens = begin(port, fingerprint, stack)
        # Kill while both authenticated players are still attached. No leave,
        # winner claim, or finalization is allowed to precede this crash.
        with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
            assert db.execute('SELECT e.status FROM issued_battles b JOIN battle_status_events e '
                              'ON e.battle_id=b.id AND e.revision=b.revision WHERE b.id=?', (identity,)).fetchone() == (1,)
            assert db.execute('SELECT count(*) FROM verified_match_results').fetchone()[0] == 0
        process.kill()
        process.wait(timeout=10)
    with server(binary, data, root) as (port, fingerprint, process), contextlib.ExitStack() as stack:
        alice, bob = [stack.enter_context(contextlib.closing(login(port, fingerprint, user))) for user in ('Alice', 'Bob')]
        protocol.request(alice, 'CrusadesGetBattleStatus', 1, auth.field(identity))
        reply = protocol.response(alice, 'CrusadesBattleStatus', 1)
        assert reply.field() == b'synthetic' and reply.field() == identity.encode()
        reply.num('<Q'); reply.num('<I')
        assert reply.num('<B') == 2
        reply.field(); reply.num('<Q')
        assert reply.num('<I') == 0 and reply.num('<B') == 0
        rejoin(alice, old_room, tokens[0], b'game not found')
        # Both account reservations were released, permitting a fresh offer.
        offered, room, _, _ = battle.issue(alice)
        assert battle.result(bob)[0] == offered and offered != identity
        assert room > 0
        # Leave this Issued battle attached when the service is killed as well.
        process.kill()
        process.wait(timeout=10)
    with server(binary, data, root), contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
        statuses = db.execute('SELECT e.status FROM issued_battles b JOIN battle_status_events e '
                              'ON e.battle_id=b.id AND e.revision=b.revision ORDER BY b.id').fetchall()
        assert statuses == [(2,), (2,)], statuses
        assert db.execute('SELECT count(*) FROM verified_match_results').fetchone()[0] == 0
        assert db.execute('SELECT revision FROM campaigns').fetchone()[0] == 0
        assert db.execute('SELECT count(*) FROM campaign_events').fetchone()[0] == 1
        assert db.execute('SELECT action,actor,before_status,after_status FROM admin_events '
            'WHERE action=? ORDER BY sequence', ('recover-battle',)).fetchall() == [
                ('recover-battle', 'takserver', 1, 2), ('recover-battle', 'takserver', 0, 2)]


def restore_service(binary, data, admin, root):
    def read_service(port, fingerprint, identity):
        with contextlib.closing(login(port, fingerprint, 'ALICE')) as alice:
            allegiance = auth.request(alice)
            protocol.request(alice, 'CrusadesGetSnapshot', 101,
                auth.field('synthetic') + struct.pack('<Q', protocol.ABSENT))
            snapshot = protocol.response(alice, 'CrusadesCampaignSnapshot', 101).data
            protocol.request(alice, 'CrusadesGetBattleStatus', 102, auth.field(identity))
            status = protocol.response(alice, 'CrusadesBattleStatus', 102).data
            return allegiance, snapshot, status

    with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
        identity, payload, digest = db.execute('SELECT battle_id,payload,replay_digest FROM verified_match_results').fetchone()
    with server(binary, data, root) as (port, fingerprint, _):
        expected = read_service(port, fingerprint, identity)
        active_backup = root / 'active-backup.sqlite'
        refused = subprocess.run([str(admin), str(root / 'campaign.sqlite'), 'backup', str(active_backup)],
            capture_output=True, timeout=20)
        assert refused.returncode != 0 and not active_backup.exists()
    backup = root / 'backup.sqlite'
    subprocess.run([str(admin), str(root / 'campaign.sqlite'), 'backup', str(backup)],
        check=True, capture_output=True, timeout=30)
    restored = root / 'restored'
    restored.mkdir()
    shutil.copy2(backup, restored / 'campaign.sqlite')
    shutil.copy2(root / 'accounts.conf', restored / 'accounts.conf')
    shutil.copy2(root / 'synthetic.campaign', restored / 'synthetic.campaign')
    shutil.copytree(root / 'crusades-replays', restored / 'crusades-replays')
    subprocess.run([str(admin), str(restored / 'campaign.sqlite'), 'health'],
        check=True, capture_output=True, timeout=30)
    with server(binary, data, restored) as (port, fingerprint, _), contextlib.ExitStack() as stack:
        assert read_service(port, fingerprint, identity) == expected
        results.verify_replay(restored, identity, digest, fingerprint)
        with contextlib.closing(sqlite3.connect(restored / 'campaign.sqlite')) as db:
            assert db.execute('SELECT payload FROM verified_match_results WHERE battle_id=?', (identity,)).fetchone() == (payload,)
        alice, bob = [stack.enter_context(contextlib.closing(login(port, fingerprint, user))) for user in ('Alice', 'Bob')]
        offered, _, _, _ = battle.issue(alice)
        assert offered != identity and battle.result(bob)[0] == offered


def authored_state_bounds(binary, data, root):
    def rejected_start(database, definition):
        result = subprocess.run([str(binary), '--local', '--data', str(data), '--port', '0',
            '--accounts', str(root / 'accounts.conf'), '--crusades-db', str(database),
            '--crusades-definition', str(definition)], capture_output=True, timeout=40)
        assert result.returncode != 0 and b'listening on' not in result.stderr
        return result.stderr

    # Incompatible authored content is rejected before creating any database.
    cases = [
        'campaign 1 "' + 'x' * 129 + '" "Too long ID"\nterritory 1 "One"\n',
        'campaign 1 "invalid" "Invalid"\nterritory 1 "One"\nnative 1 "' + 'x' * 1025 + '"\n',
        'campaign 1 "invalid" "Invalid"\nterritory 1 "One"\nmap 1 "' + 'x' * 4097 + '"\n',
        'campaign 1 "invalid" "Invalid"\nterritory 1 "\ud800"\n',
        'campaign 1 "invalid" "Invalid"\n' + ''.join(
            'territory ' + str(n) + ' "' + 'x' * 1024 + '"\n' for n in range(1, 235)),
    ]
    for number, text in enumerate(cases):
        definition, database = root / ('invalid-' + str(number) + '.campaign'), root / ('invalid-' + str(number) + '.sqlite')
        definition.write_bytes(text.encode('utf-8', errors='surrogatepass'))
        rejected_start(database, definition)
        assert not database.exists()
        assert not Path(str(database) + '.service-lock').exists()

    prepare(root)
    with server(binary, data, root):
        pass
    # Construct legal model/store records representing pre-hardening authored
    # data. Put the unservable row beyond page one to exercise full startup
    # pagination; no immutable history or existing identity is overwritten.
    with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
        original = db.execute('SELECT snapshot FROM campaign_events WHERE campaign_id=?', ('synthetic',)).fetchone()[0]
        assert original[:6] == b'TAKCS1'
        name_size = struct.unpack_from('<I', original, 6)[0]
        tail = original[10 + name_size:]
        for identity in [f'a{n:03}' for n in range(65)] + ['zz-legacy']:
            name = 'x' * 1025 if identity == 'zz-legacy' else 'One'
            definition = f'campaign 1 "{identity}" "Legacy"\nterritory 1 "{name}"\n'
            encoded_id = identity.encode()
            snapshot = b'TAKCS1' + struct.pack('<I', len(encoded_id)) + encoded_id + tail
            db.execute('INSERT INTO campaigns VALUES(?,?,0)', (identity, definition))
            db.execute('INSERT INTO campaign_rules VALUES(?,?)', (identity, 'historical-darien-v1'))
            db.execute('INSERT INTO campaign_events VALUES(?,0,?,NULL,?)', (identity, 'legacy authored fixture', snapshot))
        db.commit()
    before = (root / 'campaign.sqlite').read_bytes()
    reason = rejected_start(root / 'campaign.sqlite', root / 'synthetic.campaign')
    assert b'campaign string exceeds limit' in reason, reason
    assert (root / 'campaign.sqlite').read_bytes() == before
    with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
        assert db.execute('SELECT count(*) FROM admin_events').fetchone()[0] == 1
        assert db.execute('SELECT count(*) FROM campaigns').fetchone()[0] == 67


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--admin', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='tak-crusades-hardening-') as temporary:
        root = Path(temporary)
        prepare(root)
        boundaries(args.server.resolve(), args.data.resolve(), root)
    with tempfile.TemporaryDirectory(prefix='tak-crusades-interrupted-') as temporary:
        root = Path(temporary)
        prepare(root)
        interrupted_battle(args.server.resolve(), args.data.resolve(), root)
    with tempfile.TemporaryDirectory(prefix='tak-crusades-reconnect-') as temporary:
        root = Path(temporary)
        prepare(root)
        reconnect(args.server.resolve(), args.data.resolve(), root)
        if args.admin:
            restore_service(args.server.resolve(), args.data.resolve(), args.admin.resolve(), root)
    with tempfile.TemporaryDirectory(prefix='tak-crusades-authored-bounds-') as temporary:
        authored_state_bounds(args.server.resolve(), args.data.resolve(), Path(temporary))
    print('PASS: protocol/SCRAM replay boundaries, shared quotas, admission/login deadlines, token rotation/frozen reconnect, crash recovery without credit')


if __name__ == '__main__':
    main()
