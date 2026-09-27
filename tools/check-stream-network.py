#!/usr/bin/env python3
"""Local-only RTMPS reconnect + certificate tests. Needs ffmpeg/openssl on PATH.
Usage: python3 tools/check-stream-network.py build-o2/stream_network_test
These executables are test tools, never runtime dependencies of the game.
"""
import pathlib
import queue
import select
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time


def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def certificate(root, name):
    cert, key = root / (name + '.crt'), root / (name + '.key')
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                    '-keyout', str(key), '-out', str(cert), '-days', '1',
                    '-subj', '/CN=' + name, '-addext', 'subjectAltName=DNS:' + name],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return cert, key


class Proxy:
    def __init__(self, cert, key, backend):
        self.backend = backend
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.load_cert_chain(cert, key)
        self.socket = socket.socket()
        self.socket.bind(('127.0.0.1', 0))
        self.port = self.socket.getsockname()[1]
        self.socket.listen()
        self.socket.settimeout(.2)
        self.done = False
        self.accepted = 0
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.done:
            try:
                raw, _ = self.socket.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(target=self.forward, args=(raw,), daemon=True).start()

    def forward(self, raw):
        try:
            raw.settimeout(3)
            with self.context.wrap_socket(raw, server_side=True) as client:
                self.accepted += 1
                with socket.create_connection(('127.0.0.1', self.backend), timeout=3) as server:
                    while not self.done:
                        ready, _, _ = select.select([client, server], [], [], .2)
                        if client.pending() and client not in ready:
                            ready.append(client)
                        for source in ready:
                            data = source.recv(65536)
                            if not data:
                                return
                            (server if source is client else client).sendall(data)
        except (OSError, ssl.SSLError):
            raw.close()

    def close(self):
        self.done = True
        self.socket.close()
        self.thread.join(timeout=1)


def launch(exe, proxy, cert, seconds):
    return subprocess.Popen([exe, f'rtmps://localhost:{proxy.port}/live/test-key',
                             str(cert), str(seconds)], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)


def stop(p):
    if p and p.poll() is None:
        p.terminate()
        try:
            p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()


def main():
    exe = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix='tak-rtmps-') as directory:
        root = pathlib.Path(directory)
        cert, key = certificate(root, 'localhost')
        backend = port()
        def receiver(name):
            return subprocess.Popen(['ffmpeg', '-v', 'error', '-listen', '1', '-i',
                f'rtmp://127.0.0.1:{backend}/live/test-key', '-c', 'copy', '-y', str(root / name)],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        server = receiver('first.flv')
        proxy = Proxy(cert, key, backend)
        process = None
        try:
            time.sleep(.3)
            process = launch(exe, proxy, cert, 16)
            lines = queue.Queue()
            def reader():
                for line in process.stdout:
                    lines.put(line.strip())
            threading.Thread(target=reader, daemon=True).start()
            live = 0
            retry = False
            limit = time.monotonic() + 20
            while time.monotonic() < limit:
                try:
                    line = lines.get(timeout=.2)
                except queue.Empty:
                    if process.poll() is not None:
                        break
                    continue
                print(line, flush=True)
                if line.startswith('RECONNECTING'):
                    retry = True
                if line.startswith('LIVE'):
                    live += 1
                    if live == 1:
                        time.sleep(2)
                        stop(server)
                        server = receiver('second.flv')
                    if live == 2:
                        break
            assert live == 2 and retry, 'did not resume RTMPS after disconnect'
            process.wait(timeout=18)
            assert process.returncode == 0
            assert (root / 'first.flv').stat().st_size > 10000
            stop(server)
            assert (root / 'second.flv').stat().st_size > 10000
        finally:
            stop(process)
            stop(server)
            proxy.close()
        # A trusted certificate for a different hostname must still be rejected.
        wrong_cert, wrong_key = certificate(root, 'wrong.invalid')
        proxy = Proxy(wrong_cert, wrong_key, backend)
        try:
            p = launch(exe, proxy, wrong_cert, 3)
            output, errors = p.communicate(timeout=8)
            assert 'LIVE' not in output, 'hostname mismatch accepted'
            assert 'RECONNECTING' in output, output
            assert proxy.accepted == 0, 'TLS handshake accepted a mismatched hostname'
            assert 'test-key' not in output + errors, 'stream key leaked into logs'
            print('PASS: encrypted upload, reconnect, hostname rejection, key redaction')
        finally:
            proxy.close()


if __name__ == '__main__':
    main()
