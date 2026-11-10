#!/usr/bin/env python3
"""The server's game limits refuse a request; they never end the connection.

A --no-auth loopback server with --max-games 2 and --max-running-games 1, at protocol
level:

  create   with two games open, a third CreateGame is answered JoinResult(0, "server game
           capacity reached") -- not a Reject, which ends the connection at the client --
           and the connection stays usable: a ListGames after it is answered, and once a
           game has gone the same connection's CreateGame opens one.
  start    with one game running, the host of a second, ready room presses START: the
           server answers with the start refusal the room shows (MapError naming the room,
           "server running-game capacity reached; retry later"), the room is still there
           with its host and still answers it, and once the running game has ended the
           same START starts the game.
"""
import argparse
from pathlib import Path
import tempfile
import time
import traceback

import server_net as auth
from server_net import field, lobby
from server_disconnect_test import pump_all, recipe
from server_host_test import Seat, close_all, create, options, ready_and_start, started
from server_public_test import server

MAX_GAMES, MAX_RUNNING = 2, 1


def kinds(p, *names):
    """How many messages of each named kind the peer holds unread."""
    return {n: len(p.other.get(auth.MSG[n], [])) for n in names}


def scenario_create(port, h):
    alice, bob, carol = Seat(port, h, 'Alice'), Seat(port, h, 'Bob'), Seat(port, h, 'Carol')
    try:
        create(alice, 'one')
        create(bob, 'two')
        # The server is at its game limit: the third CreateGame is refused like any other request.
        carol.send('CreateGame', field('three') + field('') + field(recipe()) + field('') + options() + bytes([2, 0, 0]))
        assert carol.pump(10, lambda p: p.other.get(auth.MSG['JoinResult']) or p.other.get(auth.MSG['Reject']) or p.closed), \
            'the CreateGame at the game limit was not answered'
        assert not carol.other.get(auth.MSG['Reject']), \
            'the game limit was answered with a Reject (%r), which ends the connection' % \
            auth.Reader(carol.other[auth.MSG['Reject']][0]).field()
        r = auth.Reader(carol.other[auth.MSG['JoinResult']].pop(0))
        ok, _slot, why = r.num('<B'), r.num('<B'), r.field()
        assert ok == 0 and why == b'server game capacity reached', ('the refusal', ok, why)
        # The connection stays usable: the list is answered (both games, neither Carol's).
        assert len(carol.games()) == MAX_GAMES and not carol.closed, 'the refused connection no longer lists games'
        assert not carol.other.get(auth.MSG['LobbyState']), 'a refused create was sent a room'
        # Room for one more once a game has gone: the same connection's CreateGame opens it.
        bob.send('LeaveGame')
        assert pump_all((alice, bob, carol), 10, lambda: len(carol.games()) == MAX_GAMES - 1), "Bob's game never closed"
        room = create(carol, 'three')
        assert room in bob.games(), "Carol's game is not listed"   # (a connection in a room is sent no list)
    finally:
        close_all(alice, bob, carol)


def scenario_start(port, h):
    # (Dave stays in the game list: a connection in a room is sent no list.)
    alice, carol, dave = Seat(port, h, 'Alice'), Seat(port, h, 'Carol'), Seat(port, h, 'Dave')
    try:
        # Alice's game runs: the server is at its running-game limit.
        first = create(alice, 'running')
        alice.slot = 0
        ready_and_start(alice, (alice,), ai=(1,))
        started(alice, h)
        assert alice.pump(20, lambda p: p.last_tick() >= 15), "Alice's game never ticked"
        # Carol's room is ready; her START is refused, and nothing else happens to her.
        second = create(carol, 'waiting')
        carol.slot = 0
        ready_and_start(carol, (carol,), ai=(1,))
        answered = ('MapError', 'Reject', 'GameStarting')
        assert carol.pump(10, lambda p: any(kinds(p, *answered).values()) or p.closed), 'the START at the limit was not answered'
        got = kinds(carol, *answered)
        assert not got['Reject'], 'the running-game limit was answered with a Reject (%r), which ends the connection' % \
            auth.Reader(carol.other[auth.MSG['Reject']][0]).field()
        assert got == {'MapError': 1, 'Reject': 0, 'GameStarting': 0} and not carol.closed, ('the answer to START', got)
        r = auth.Reader(carol.other[auth.MSG['MapError']].pop(0))
        assert r.num('<I') == second and r.field() == b'server running-game capacity reached; retry later', \
            'the start refusal does not name the room and the reason'
        # The room is still there, not started, and still its host's: it answers an edit.
        assert dave.games() == {first: 1, second: 0}, "Carol's room is gone, or runs"
        seen = len(carol.other.get(auth.MSG['LobbyState'], []))
        auth.slot_update(carol, 0, 1, 2, 0, 0, 1, 2)
        assert carol.pump(5, lambda p: len(p.other.get(auth.MSG['LobbyState'], [])) > seen), 'the room no longer answers its host'
        state = lobby(auth.Reader(carol.other[auth.MSG['LobbyState']][-1]))
        assert state[0] == second and state[3][0][0][0] == 1 and state[3][0][0][1] == 2 and state[3][1][0][0] == 2, \
            ('the room after the refused START', state)
        # Alice's game ends; the same START now starts Carol's.
        alice.send('LeaveGame')
        assert pump_all((alice, carol, dave), 15, lambda: first not in dave.games()), "Alice's game never ended"
        carol.send('StartGame')
        started(carol, h)
        assert carol.pump(20, lambda p: p.last_tick() >= 15), "Carol's game never ticked"
        assert dave.games() == {second: 1}, "Carol's game is not listed as running"
    finally:
        close_all(alice, carol, dave)


CASES = [('create', scenario_create), ('start', scenario_start)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--server', type=Path, required=True)
    ap.add_argument('--data', type=Path, required=True)
    a = ap.parse_args()
    failed = []
    for name, case in CASES:
        # A server of its own per case: no game of one counts against the other's limits.
        with tempfile.TemporaryDirectory(prefix='tak-limits-') as tmp:
            root = Path(tmp)
            with server(a.server.resolve(), a.data.resolve(), root, '--no-auth', '--max-games', str(MAX_GAMES),
                        '--max-running-games', str(MAX_RUNNING)) as (port, h, process):
                try:
                    case(port, h)
                    assert process.poll() is None, 'the server stopped'
                    print('PASS: %s' % name)
                except Exception:   # an assertion, or a connection the server ended
                    failed.append(name)
                    print('FAIL: %s\n%s' % (name, traceback.format_exc()))
                    time.sleep(0.2)
                    print((root / 'log').read_text(errors='replace')[-1500:])
    if failed:
        raise SystemExit('FAIL: ' + ', '.join(failed))
    print('PASS: the game limit refuses CreateGame with JoinResult(0) and the running-game limit refuses START '
          'with a start refusal; both keep the connection, and the request passes once there is room')


if __name__ == '__main__':
    main()
