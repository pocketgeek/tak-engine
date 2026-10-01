#!/usr/bin/env python3
"""Exercise public admission and work budgets on a real authenticated server."""
import argparse
import contextlib
from pathlib import Path
import re
import socket
import ssl
import struct
import subprocess
import tempfile
import time
import crusades_auth_network_test as auth
from crusades_battle_network_test import Peer

@contextlib.contextmanager
def server(binary, data, root, *options):
    with socket.socket() as s:
        s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    with (root/'log').open('w') as log:
        p=subprocess.Popen([str(binary),'--local','--data',str(data),'--port',str(port),
            '--accounts',str(root/'accounts'),*options],stdout=log,stderr=log)
        try:
            deadline=time.monotonic()+40
            while time.monotonic()<deadline:
                text=(root/'log').read_text()
                if 'listening on' in text:
                    yield port,int(re.search(r'retail gameplay hash ([0-9a-f]+)',text)[1],16),p
                    return
                assert p.poll() is None,text
                time.sleep(.05)
            raise AssertionError('server startup timeout')
        finally:
            p.terminate()
            try:p.wait(timeout=10)
            except subprocess.TimeoutExpired:p.kill();p.wait()

def login(port, fingerprint, name):
    p=Peer(port);p.hello(fingerprint);p.login(name,[]);return p

def options(stress=0,benchmark=0):
    return bytes([0,1,0,10,0])+struct.pack('<I',2000)+bytes([0,stress,0,benchmark,0,0])

def create(p,stress=0,benchmark=0,mission=''):
    p.send('CreateGame',auth.field('test')+auth.field('')+auth.field('missing-test-map')+
        auth.field(mission)+options(stress,benchmark)+bytes([8,0,0]))

def room(p):
    r=p.receive('JoinResult');assert r.num('<B')==1;r.num('<B');r.field()
    return p.receive('LobbyState').num('<I')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--server',type=Path,required=True);ap.add_argument('--data',type=Path,required=True);ap.add_argument('--tls-tool',type=Path,required=True);a=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='tak-public-') as tmp:
        root=Path(tmp)
        with server(a.server.resolve(),a.data.resolve(),root,'--max-games','1','--map-memory-mib','1') as (port,h,process):
            with contextlib.closing(login(port,h,'Alice')) as p,contextlib.closing(login(port,h,'Bob')) as q:
                for stress,bench,mission in [(1,0,''),(0,1,''),(0,0,'mission')]:
                    create(p,stress,bench,mission)
                    assert b'public servers' in p.receive('Reject').field()
                create(p);rid=room(p)
                create(q);assert b'capacity' in q.receive('Reject').field()
                p.send('SetGameOptions',options(benchmark=1))
                assert b'disabled' in p.receive('Reject').field()
                p.send('MapOffer',struct.pack('<I',rid)+auth.field('missing-test-map')+auth.field('0'*64)+struct.pack('<I',1<<20))
                error=p.receive('MapError');assert error.num('<I')==rid;assert b'budget' in error.field()
                # A smaller offer is admitted; mere offers reserve aggregate memory.
                p.send('MapOffer',struct.pack('<I',rid)+auth.field('missing-test-map')+auth.field('0'*64)+struct.pack('<I',1000))
                assert p.receive('MapRequest').num('<I')==rid
                q.send('ListGames');assert q.receive('GameList').num('<I')==1
                p.send('LeaveGame');p.send('Ping');p.receive('Pong')
                create(q);room(q) # capacity and upload reservation released on leave
                assert process.poll() is None
        with server(a.server.resolve(),a.data.resolve(),root,'--closed-registration') as (port,h,process):
            with contextlib.closing(login(port,h,'Alice')) as p:
                p.send('ListGames');assert p.receive('GameList').num('<I')==0
                # Sub-poll-size floods must still hit a wall-clock budget.
                p.socket.sendall((struct.pack('<IB',1,auth.MSG['ListGames']))*300)
                p.socket.settimeout(3)
                try:
                    while p.socket.recv(65536):pass
                except ConnectionResetError:pass
            with contextlib.closing(Peer(port)) as p:
                p.hello(h);p.send('AuthBegin',auth.field('Carol')+auth.field(bytes(32)))
                p.receive('AuthChallenge');p.send('AuthRegister',auth.field(bytes(32))+auth.field(bytes(32)))
                r=p.receive('AuthResult');assert r.num('<B') not in (0,1)
                assert process.poll() is None
        with server(a.server.resolve(),a.data.resolve(),root,'--max-accounts','2') as (port,h,process):
            with contextlib.closing(Peer(port)) as p:
                p.hello(h);p.send('AuthBegin',auth.field('Carol')+auth.field(bytes(32)))
                p.receive('AuthChallenge');p.send('AuthRegister',auth.field(bytes(32))+auth.field(bytes(32)))
                assert p.receive('AuthResult').num('<B') not in (0,1)
                assert process.poll() is None
        subprocess.run([str(a.tls_tool.resolve()),'--fixtures',str(root)],check=True)
        context=ssl.create_default_context(cafile=str(root/'cert.pem'))
        with server(a.server.resolve(),a.data.resolve(),root,'--tls-cert',str(root/'cert.pem'),'--tls-key',str(root/'key.pem')) as (port,h,process):
            with contextlib.closing(Peer(port)) as p:
                p.socket=context.wrap_socket(p.socket,server_hostname='localhost')
                p.hello(h);p.login('Alice',[])
                p.send('ListGames');assert p.receive('GameList').num('<I')==0
                p.send('Ping');p.receive('Pong')
                assert process.poll() is None
        denied=subprocess.run([str(a.server.resolve()),'--data',str(a.data.resolve())],capture_output=True,timeout=10)
        assert denied.returncode!=0 and b'public listeners require' in denied.stderr
        for value in ['0','-1','not-a-number','18446744073709551616']:
            denied=subprocess.run([str(a.server.resolve()),'--max-games',value,'--local','--data',str(a.data.resolve())],capture_output=True,timeout=10)
            assert denied.returncode!=0 and b'invalid server resource limit' in denied.stderr
    print('PASS: public game modes, room admission, upload reservation, registration, request floods and ordinary lobby traffic, TLS login and secure startup defaults')
if __name__=='__main__':main()
