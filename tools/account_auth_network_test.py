#!/usr/bin/env python3
"""Exercise the real server's account login over loopback with SCRAM.

Requires a retail installation; creates only synthetic temporary accounts.
"""
import argparse
import contextlib
import os
from pathlib import Path
import struct
import tempfile

import server_net as net


def run(binary, data):
    secrets = []
    with tempfile.TemporaryDirectory(prefix='tak-account-auth-') as temporary:
        root = Path(temporary)
        with net.server(binary, data, root) as (port, fingerprint):
            with contextlib.closing(net.Peer(port)) as alice:
                alice.hello(fingerprint)
                alice.login('Alice', secrets)            # unknown name: registers (status 1)
            with contextlib.closing(net.Peer(port)) as alice:
                alice.hello(fingerprint)
                alice.login('ALICE', secrets)            # known name, any case: logs in (status 0)
            with contextlib.closing(net.Peer(port)) as failed:
                failed.hello(fingerprint)
                failed.login('Alice', secrets, wrong=True)   # wrong proof: status 2
            with contextlib.closing(net.Peer(port)) as pending:
                pending.hello(fingerprint)
                pending.send('AuthBegin', net.field('NotRegistered') + net.field(os.urandom(32)))
                challenge = pending.receive('AuthChallenge')
                assert challenge.num('<B') == 1, 'an unregistered name was not offered registration'
        # The account survives a restart.
        with net.server(binary, data, root) as (port, fingerprint):
            with contextlib.closing(net.Peer(port)) as alice:
                alice.hello(fingerprint)
                alice.login('Alice', secrets)
        stored = (root / 'accounts.conf').read_bytes()
        password = secrets[0]
        assert password not in stored, 'the password reached the accounts file'
    print('PASS: live SCRAM register, login, wrong proof, pending registration and restart')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    args = parser.parse_args()
    run(args.server.resolve(), args.data.resolve())


if __name__ == '__main__':
    main()
