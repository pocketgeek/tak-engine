#!/usr/bin/env python3
"""Exercise the real server's campaign boundary over loopback with SCRAM.

Requires a retail installation; creates only synthetic temporary accounts/data.
"""
import argparse
import contextlib
import hashlib
import hmac
import os
from pathlib import Path
import re
import socket
import sqlite3
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / 'src/net/protocol.h').read_text()
VERSION = int(re.search(r'kNetVersion = (\d+)', header)[1])
enum = re.search(r'enum class Msg[^\{]*\{(.*?)\};', header, re.S)[1]
enum = re.sub(r'//[^\n]*', '', enum)
MSG, value = {}, 0
for entry in enum.split(','):
    entry = entry.strip()
    if not entry:
        continue
    parts = entry.split('=')
    value = int(parts[1].strip()) if len(parts) == 2 else value + 1
    MSG[parts[0].strip()] = value


def field(value):
    value = value.encode() if isinstance(value, str) else value
    return struct.pack('<H', len(value)) + value


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def num(self, fmt):
        size = struct.calcsize(fmt)
        value = struct.unpack_from(fmt, self.data, self.pos)[0]
        self.pos += size
        return value

    def field(self):
        size = self.num('<H')
        value = self.data[self.pos:self.pos + size]
        self.pos += size
        assert len(value) == size
        return value


class Peer:
    def __init__(self, port):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=10)

    def close(self):
        self.socket.close()

    def send(self, kind, payload=b''):
        self.socket.sendall(struct.pack('<IB', len(payload) + 1, MSG[kind]) + payload)

    def exact(self, size):
        data = b''
        while len(data) < size:
            chunk = self.socket.recv(size - len(data))
            if not chunk:
                raise RuntimeError('server closed connection')
            data += chunk
        return data

    def receive(self, kind):
        while True:
            length = struct.unpack('<I', self.exact(4))[0]
            assert 1 <= length <= 16 * 1024 * 1024
            body = self.exact(length)
            if body[0] == MSG['Ping']:
                self.send('Pong')
                continue
            assert body[0] == MSG[kind], (kind, body[0])
            return Reader(body[1:])

    def hello(self, data_hash):
        self.send('Hello', struct.pack('<I', VERSION) + field('campaign-test') +
                  struct.pack('<Q', data_hash) + field('SomeoneElse'))
        self.receive('AuthRequired')

    def login(self, user, secrets, wrong=False):
        nonce = os.urandom(32)
        self.send('AuthBegin', field(user) + field(nonce))
        challenge = self.receive('AuthChallenge')
        new = challenge.num('<B')
        salt, iterations = challenge.field(), challenge.num('<I')
        server_nonce = challenge.field()
        password = b'local synthetic campaign test password'
        salted = hashlib.pbkdf2_hmac('sha256', password, salt, iterations)
        client_key = hmac.digest(salted, b'Client Key', 'sha256')
        stored_key = hashlib.sha256(client_key).digest()
        server_key = hmac.digest(salted, b'Server Key', 'sha256')
        pieces = [b'TAK-SCRAM-SHA-256-v1', user.encode(), nonce, server_nonce, salt]
        transcript = b''.join(struct.pack('>I', len(x)) + x for x in pieces) + struct.pack('>I', iterations)
        secrets.extend([password, salt, stored_key, server_key])
        if new:
            assert not wrong
            self.send('AuthRegister', field(stored_key) + field(server_key))
        else:
            signature = hmac.digest(stored_key, transcript, 'sha256')
            proof = bytes(a ^ b for a, b in zip(client_key, signature))
            self.send('AuthProof', field(bytes(32) if wrong else proof))
        reply = self.receive('AuthResult')
        status, signature = reply.num('<B'), reply.field()
        if wrong:
            assert status == 2
            return
        assert status == (1 if new else 0)
        assert hmac.compare_digest(signature, hmac.digest(server_key, transcript, 'sha256'))
        welcome = self.receive('Welcome')
        welcome.num('<I')
        assert welcome.field().lower() == user.lower().encode()


@contextlib.contextmanager
def server(binary, data, root, enabled=True):
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    command = [str(binary), '--local', '--data', str(data), '--port', str(port),
               '--accounts', str(root / 'accounts.conf')]
    if enabled:
        command += ['--crusades-db', str(root / 'campaign.sqlite'),
                    '--crusades-definition', str(root / 'synthetic.campaign')]
    with (root / 'server.log').open('w') as log:
        process = subprocess.Popen(command, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                text = (root / 'server.log').read_text(errors='replace')
                if 'listening on' in text:
                    fingerprint = re.search(r'retail gameplay hash ([0-9a-f]+)', text)
                    yield port, int(fingerprint[1], 16)
                    break
                if process.poll() is not None:
                    raise RuntimeError('server startup failed: ' + text)
                time.sleep(.05)
            else:
                raise RuntimeError('server startup timed out')
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def request(peer, revision=None, alliance=0, campaign='synthetic', extra=b'', status=0):
    kind = 'CrusadesGetAllegiance' if revision is None else 'CrusadesSetAllegiance'
    payload = field(campaign)
    if revision is not None:
        payload += struct.pack('<QB', revision, alliance)
    peer.send(kind, payload + extra)
    r = peer.receive('CrusadesAllegianceResult')
    operation, actual = r.num('<B'), r.num('<B')
    returned_campaign, side = r.field(), r.num('<B')
    rev, joined, changed = r.num('<Q'), r.num('<Q'), r.num('<Q')
    r.field()
    assert r.pos == len(r.data)
    assert operation == (revision is not None) and actual == status, (kind, actual, status)
    if status == 0:
        assert returned_campaign == campaign.encode()
    return side, rev, joined, changed


def run(binary, data):
    secrets = []
    absent = 2**64 - 1
    with tempfile.TemporaryDirectory(prefix='tak-crusades-auth-') as temporary:
        root = Path(temporary)
        definition = root / 'synthetic.campaign'
        definition.write_text('campaign 1 "synthetic" "Synthetic"\nterritory 1 "One"\n')
        with server(binary, data, root) as (port, fingerprint):
            with contextlib.closing(Peer(port)) as alice:
                request(alice, absent, 1, status=2)  # Not even Hello completed.
                alice.hello(fingerprint)
                request(alice, absent, 1, status=2)  # Hello name is not identity.
                alice.login('Alice', secrets)
                assert request(alice)[:2] == (0, absent)
                complete = field('synthetic') + struct.pack('<QB', absent, 1)
                for length in range(len(complete)):
                    alice.send('CrusadesSetAllegiance', complete[:length])
                    malformed = alice.receive('CrusadesAllegianceResult')
                    assert malformed.num('<B') == 1 and malformed.num('<B') == 1
                first = request(alice, absent, 1)
                assert first[:2] == (1, 0) and first[2] > 0
                request(alice, absent, 2, status=4)  # Replayed join.
                request(alice, 0, 0, status=1)      # Contested is not allegiance.
                request(alice, 0, 2, extra=field('Bob'), status=1)  # Identity injection.
                request(alice, 0, 2, campaign='unknown', status=4)
                request(alice, 0, 2, campaign='x' * 129, status=1)
                assert request(alice)[:2] == (1, 0)
                switched = request(alice, 0, 2)
                assert switched[:2] == (2, 1) and switched[2] == first[2]
                request(alice, 1, 2, status=4)  # Duplicate same-side mutation.
            with contextlib.closing(Peer(port)) as failed:
                failed.hello(fingerprint)
                failed.login('Alice', secrets, wrong=True)
                request(failed, 1, 1, status=2)
            with contextlib.closing(Peer(port)) as bob:
                bob.hello(fingerprint)
                bob.login('Bob', secrets)
                assert request(bob)[:2] == (0, absent)
                assert request(bob, absent, 1)[:2] == (1, 0)
            with contextlib.closing(Peer(port)) as pending:
                pending.hello(fingerprint)
                pending.send('AuthBegin', field('NotRegistered') + field(os.urandom(32)))
                pending.receive('AuthChallenge')
                request(pending, absent, 1, status=2)
        # Real process restart retains allegiance, case-insensitive account identity,
        # and validates an identical bootstrap definition without resetting state.
        with server(binary, data, root) as (port, fingerprint):
            with contextlib.closing(Peer(port)) as alice:
                alice.hello(fingerprint)
                alice.login('ALICE', secrets)
                assert request(alice)[:2] == (2, 1)
        before = (root / 'campaign.sqlite').read_bytes()
        with server(binary, data, root, enabled=False) as (port, fingerprint):
            with contextlib.closing(Peer(port)) as alice:
                alice.hello(fingerprint)
                alice.login('Alice', secrets)
                request(alice, 1, 1, status=3)
        assert before == (root / 'campaign.sqlite').read_bytes()
        with contextlib.closing(sqlite3.connect(root / 'campaign.sqlite')) as db:
            assert db.execute('SELECT account_id,revision FROM campaign_participants ORDER BY account_id').fetchall() == [('alice', 1), ('bob', 0)]
            assert db.execute('SELECT count(*) FROM allegiance_events').fetchone()[0] == 3
            assert db.execute('SELECT revision FROM campaigns').fetchone()[0] == 0
        for secret in secrets:
            assert secret not in before and secret.hex().encode() not in before, 'credential material entered campaign DB'
        # Unsafe combinations must fail before creating or modifying campaign data.
        invalid = [
            ['--no-auth', '--crusades-db', str(root / 'forbidden.sqlite')],
            ['--accounts', str(root / 'accounts.conf'), '--crusades-db', str(root / 'accounts.conf')],
            ['--accounts', str(root / 'sidecar.sqlite-journal'), '--crusades-db', str(root / 'sidecar.sqlite')],
        ]
        for flags in invalid:
            result = subprocess.run([str(binary), '--data', str(data), *flags],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
            assert result.returncode != 0
        assert not (root / 'forbidden.sqlite').exists()
    print('PASS: live SCRAM allegiance authorization, identity isolation, restart and credential exclusion')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    args = parser.parse_args()
    run(args.server.resolve(), args.data.resolve())


if __name__ == '__main__':
    main()
