#!/usr/bin/env python3
"""Voice to a listener on a link too slow for it must not hold up the game (protocol 276).

Alice and Carol are seated on fast links; Bob is seated and Dave spectates, both reading
their sockets at 1500 bytes a second with a 4 KiB receive buffer -- room for the game's
tick bundles (~500 B/s) but not for voice (~3.8 kB/s for one talker). Alice talks to
everyone (mask 0xff) for 12 seconds.

Voice shares each listener's TCP stream with its tick bundles. Before the server's lag
check, voice filled the slow links and every bundle waited behind it: Bob's delay held
the whole room to ~2 ticks a second, and the spectator fell further behind every second
(180 ticks after 12 s). Now:

  room      the room keeps at least 80% of its 30 ticks a second while Alice talks
  lag       neither slow listener ever falls more than 60 ticks (2 s) behind the room,
            and both are back within 10 ticks once she stops
  voice     voice still reached them while their links had room (it is paused, not
            switched off), and the server logged the pause
"""
import argparse
import contextlib
from pathlib import Path
import socket
import struct
import tempfile
import threading
import time

import crusades_auth_network_test as auth
from crusades_battle_network_test import lobby
from server_public_test import server, login, options
from server_voice_test import Listener, up, START

RATE = 1500.0        # bytes a second each slow listener reads
TICKS = 30           # the room's nominal tick rate (speed 10)


class Slow:
    """Reads one connection at RATE bytes a second; acknowledges ticks as a client does."""

    def __init__(self, peer):
        self.peer, self.sock = peer, peer.socket
        self.sock.settimeout(None)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        self.buf = b''
        self.lastTick = 0
        self.voiceFrames = 0
        self.write = threading.Lock()
        send = peer.send

        def locked(kind, payload=b''):
            with self.write:
                send(kind, payload)
        peer.send = locked
        threading.Thread(target=self.run, daemon=True).start()

    def run(self):
        t0, got = time.monotonic(), 0
        try:
            while True:
                d = self.sock.recv(256)
                if not d:
                    return
                got += len(d)
                self.buf += d
                while len(self.buf) >= 4:
                    n = struct.unpack_from('<I', self.buf)[0]
                    if len(self.buf) < 4 + n:
                        break
                    body, self.buf = self.buf[4:4 + n], self.buf[4 + n:]
                    if body[0] == auth.MSG['Ping']:
                        self.peer.send('Pong')
                    elif body[0] == auth.MSG['TickBundle']:
                        self.lastTick = struct.unpack_from('<I', body, 1)[0]
                        if self.lastTick % 4 == 0:
                            self.peer.send('Progress', struct.pack('<II', self.lastTick, 20))
                    elif body[0] == auth.MSG['VoiceDown']:
                        self.voiceFrames += 1
                dt = t0 + got / RATE - time.monotonic()
                if dt > 0:
                    time.sleep(dt)
        except OSError:
            return


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--server', type=Path, required=True)
    ap.add_argument('--data', type=Path, required=True)
    a = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='tak-voice-slow-') as tmp:
        root = Path(tmp)
        with server(a.server.resolve(), a.data.resolve(), root) as (port, h, process), contextlib.ExitStack() as stack:
            peers = {n: stack.enter_context(contextlib.closing(login(port, h, n)))
                     for n in ('Alice', 'Bob', 'Carol', 'Dave')}
            alice, bob, carol, dave = (peers[n] for n in ('Alice', 'Bob', 'Carol', 'Dave'))
            recipe = '~gen1~' + (struct.pack('<HQBHH', 8, 1234, 0, 128, 128) + bytes([2, 0, 0, 1, 0, 0, 0])).hex()
            alice.send('CreateGame', auth.field('slow') + auth.field('') + auth.field(recipe) + auth.field('') +
                       options() + bytes([4, 0, 0]))
            assert alice.receive('JoinResult').num('<B') == 1
            room = lobby(alice.receive('LobbyState'))
            rid = room[0]
            for p in (bob, carol):
                p.send('JoinGame', struct.pack('<I', rid) + auth.field(''))
                assert p.receive('JoinResult').num('<B') == 1, 'join failed'
            values = room[3][0][0]
            alice.send('SlotUpdate', bytes([0, 1, values[1], values[2], values[3], 1, values[5]]))
            bob.send('SlotUpdate', bytes([1, 1, 1, 1, 1, 1, 2]))
            carol.send('SlotUpdate', bytes([2, 1, 2, 2, 2, 1, 2]))
            B = Slow(bob)
            time.sleep(0.3)
            A, C = Listener(alice, 'Alice'), Listener(carol, 'Carol')
            alice.send('StartGame')
            assert A.wait('GameStarting') and C.wait('GameStarting'), 'the game did not start'
            for p in (alice, carol, bob):
                p.send('Loaded')
            assert A.wait('TickBundle', 5), 'the game is not ticking'
            dave.send('Spectate', struct.pack('<I', rid) + auth.field(''))
            D = Slow(dave)
            for p in (bob, dave):
                p.send('VoiceListen', b'\x01')

            roomTick = [0]

            def sample():
                t = A.take('TickBundle')
                if t:
                    roomTick[0] = struct.unpack_from('<I', t[-1], 0)[0]
                return roomTick[0], roomTick[0] - B.lastTick, roomTick[0] - D.lastTick

            time.sleep(2.0)   # settle: the spectator catches up, everyone acknowledges
            tick0, lagB, lagD = sample()
            assert lagB <= 10 and lagD <= 10, 'slow listeners behind before any voice: %d %d' % (lagB, lagD)

            seconds, worstB, worstD = 12, 0, 0
            t0 = time.monotonic()
            for i in range(seconds * 50):
                alice.send('VoiceUp', up(i, 0xff, START if i == 0 else 0))
                time.sleep(max(0.0, t0 + (i + 1) * 0.02 - time.monotonic()))
                if i % 25 == 24:
                    tick, lagB, lagD = sample()
                    worstB, worstD = max(worstB, lagB), max(worstD, lagD)
            tick1 = sample()[0]
            rate = (tick1 - tick0) / (time.monotonic() - t0)
            time.sleep(2.0)
            _, endB, endD = sample()
            print('slow: room %.1f ticks/s while talking; worst lag Bob %d Dave %d; after %d %d; voice frames %d %d'
                  % (rate, worstB, worstD, endB, endD, B.voiceFrames, D.voiceFrames))
            assert rate >= 0.8 * TICKS, 'voice to a slow seated player held the room to %.1f ticks/s' % rate
            assert worstB <= 60 and worstD <= 60, 'a slow listener fell %d / %d ticks behind' % (worstB, worstD)
            assert endB <= 10 and endD <= 10, 'a slow listener did not recover: %d / %d' % (endB, endD)
            assert B.voiceFrames >= 50 and D.voiceFrames >= 50, 'voice never reached the slow listeners'
            assert 'voice to it paused' in (root / 'log').read_text(), 'the pause was not logged'
            for L in (A, C):
                assert not L.take('Desynced') and not L.closed
            assert process.poll() is None, 'the server died'
    print('server_voice_slow: all passed')


if __name__ == '__main__':
    main()
