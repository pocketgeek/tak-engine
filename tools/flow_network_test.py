#!/usr/bin/env python3
"""Authoritative pathfinder selection on a private loopback server."""
import argparse
import contextlib
from pathlib import Path
import struct
import tempfile
import time
import crusades_auth_network_test as auth
from crusades_battle_network_test import lobby
from server_public_test import server, login, options


def selected(mode):
    result = bytearray(options())
    result[-1] = mode
    return result


def create(peer, mode):
    recipe = '~gen1~' + (struct.pack('<HQBHH', 8, 1234, 0, 128, 128) +
                         bytes([2, 0, 0, 1, 0, 0, 0])).hex()
    peer.send('CreateGame', auth.field('flow-test') + auth.field('') +
              auth.field(recipe) + auth.field('') + selected(mode) + bytes([2, 0, 0]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--server', type=Path, required=True)
    ap.add_argument('--data', type=Path, required=True)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='tak-flow-net-') as temporary:
        with server(args.server.resolve(), args.data.resolve(), Path(temporary)) as (port, fingerprint, process):
            with contextlib.closing(login(port, fingerprint, 'FlowHost')) as host, \
                 contextlib.closing(login(port, fingerprint, 'FlowGuest')) as guest:
                host.socket.settimeout(20);guest.socket.settimeout(20)
                for invalid in (4, 255):
                    create(host, invalid)
                    assert b'pathfinding' in host.receive('Reject').field()
                    host.send('ListGames')
                    assert host.receive('GameList').num('<I') == 0
                for mode in (0, 1, 2, 3):
                    # Creation/start work is deliberately rate limited by the
                    # real server. Give each mode its own one-second window.
                    time.sleep(1.05)
                    create(host, mode)
                    assert host.receive('JoinResult').num('<B') == 1
                    initial = lobby(host.receive('LobbyState'))
                    assert initial[2][-1] == mode
                    host.send('SetGameOptions', selected(4))
                    assert b'pathfinding' in host.receive('Reject').field()
                    host.send('SetGameOptions', selected((mode + 1) % 4))
                    assert lobby(host.receive('LobbyState'))[2][-1] == mode
                    guest.send('JoinGame', struct.pack('<I', initial[0]) + auth.field(''))
                    assert guest.receive('JoinResult').num('<B') == 1
                    joined = lobby(guest.receive('LobbyState'))
                    assert joined[2][-1] == mode
                    # A non-host also cannot alter the pathfinder; the next slot
                    # update acts as a sequencing barrier that publishes state.
                    guest.send('SetGameOptions', selected((mode + 1) % 4))
                    slots = joined[3]
                    for peer, index in ((host, 0), (guest, 1)):
                        value = slots[index][0]
                        peer.send('SlotUpdate', bytes([index, 1, value[1], value[2], value[3], 1, value[5]]))
                    while True:
                        state = lobby(host.receive('LobbyState'))
                        if state[3][0][0][4] and state[3][1][0][4]:
                            assert state[2][-1] == mode
                            break
                    host.send('StartGame')
                    for peer in (host, guest):
                        start = lobby(peer.receive('GameStarting'))
                        assert start[2][-1] == mode
                    with contextlib.closing(login(port, fingerprint, 'FlowSpectator')) as spectator:
                        spectator.socket.settimeout(20)
                        spectator.send('Spectate', struct.pack('<I', initial[0]) + auth.field(''))
                        state = lobby(spectator.receive('GameStarting'))
                        assert state[2][-1] == mode
                        spectator.send('LeaveGame')
                        spectator.send('Ping'); spectator.receive('Pong')
                    # Gameplay equivalence is independently checked by the
                    # lockstep client harness.
                    guest.send('LeaveGame'); host.send('LeaveGame')
                    for peer in (guest, host):
                        peer.send('Ping'); peer.receive('Pong')
                        peer.pending.clear()
                    assert process.poll() is None
    print('PASS pathfinder admission, immutable lobby mode, join, game-start and spectator propagation')


if __name__ == '__main__':
    main()
