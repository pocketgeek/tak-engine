#!/usr/bin/env python3
"""Local-only RTMPS upload test: the stream must stay LIVE and keep uploading.

The receiver is stream_network_test's own --sink (the same static FFmpeg as the game),
behind a Python TLS proxy with a throwaway localhost certificate, so this needs only
python3 and openssl -- no ffmpeg program -- and runs on macOS CI. Like YouTube, the sink
sends nothing while the first megabytes arrive. That caught 0.9.0's macOS SecureTransport
bug: RTMP's non-blocking check for server messages blocked, every upload stalled out the
5 s timeout after a few frames and went RECONNECTING. The server certificate is issued by
a throwaway root CA the client is given; a certificate for another host name and one from
a root outside the system trust store must both be refused. Usage: python3 tools/check-stream-rtmps.py build/stream_network_test [encoder]
Exits 77 (skipped) without openssl. Never contacts a real service.
"""
import pathlib
import select
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

SECONDS = 10


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def run_openssl(*args):
    subprocess.run(['openssl', *map(str, args)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def authority(root):
    """A throwaway root CA: the client trusts it as its ca_file, like a public root."""
    cert, key = root / 'ca.crt', root / 'ca.key'
    run_openssl('req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', key, '-out', cert,
                '-days', '1', '-subj', '/CN=TAK test root', '-addext', 'basicConstraints=critical,CA:TRUE',
                '-addext', 'keyUsage=critical,keyCertSign,cRLSign')
    return cert, key


def certificate(root, ca, name='localhost'):
    """A server certificate for `name` issued by `ca` (a proper chain, not self-signed)."""
    cert, key, request, ext = (root / (name + suffix) for suffix in ('.crt', '.key', '.csr', '.ext'))
    run_openssl('req', '-new', '-newkey', 'rsa:2048', '-nodes', '-keyout', key, '-out', request,
                '-subj', '/CN=' + name)
    # serverAuth: macOS rejected a TLS server certificate without it (-9807).
    ext.write_text(f'subjectAltName=DNS:{name}\nextendedKeyUsage=serverAuth\n'
                   'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n')
    run_openssl('x509', '-req', '-in', request, '-CA', ca[0], '-CAkey', ca[1], '-set_serial',
                str(2 + sum(map(ord, name))), '-days', '1', '-sha256', '-extfile', ext, '-out', cert)
    return cert, key


def refused(exe, cert, key, ca_file, encoder, why):
    """The client must refuse this server's certificate, with the TLS CERTIFICATE reason."""
    proxy = Proxy(cert, key, free_port())
    try:
        client = subprocess.run([exe, f'rtmps://localhost:{proxy.port}/live/test-key', str(ca_file),
                                 '3', encoder], capture_output=True, text=True, timeout=30)
        print(client.stdout, end='')
        assert 'LIVE' not in client.stdout and proxy.accepted == 0, why + ' accepted'
        assert 'RECONNECTING 1/3 - TLS CERTIFICATE' in client.stdout, 'no certificate reason: ' + why
        assert 'test-key' not in client.stdout + client.stderr, 'stream key leaked into logs'
    finally:
        proxy.close()


class Proxy:
    """TLS in, plain RTMP out to the sink; counts what the stream uploaded."""
    def __init__(self, cert, key, backend):
        self.backend, self.done, self.accepted, self.up = backend, False, 0, 0
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.load_cert_chain(cert, key)
        self.socket = socket.socket()
        self.socket.bind(('127.0.0.1', 0))
        self.port = self.socket.getsockname()[1]
        self.socket.listen()
        self.socket.settimeout(.2)
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
                            if source is client:
                                self.up += len(data)
        except (OSError, ssl.SSLError):
            raw.close()

    def close(self):
        self.done = True
        self.socket.close()
        self.thread.join(timeout=1)


def main():
    exe = sys.argv[1]
    encoder = sys.argv[2] if len(sys.argv) > 2 else 'libx264'
    if not shutil.which('openssl'):
        print('SKIP: openssl not found')
        return 77
    with tempfile.TemporaryDirectory(prefix='tak-rtmps-') as directory:
        root = pathlib.Path(directory)
        ca = authority(root)
        cert, key = certificate(root, ca)
        backend = free_port()
        sink = subprocess.Popen([exe, '--sink', str(backend), str(SECONDS + 10)],
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        proxy = Proxy(cert, key, backend)
        try:
            assert sink.stdout.readline().strip() == 'SINK LISTENING'
            time.sleep(1)  # the sink's listen() follows that line
            started = time.monotonic()
            client = subprocess.run([exe, f'rtmps://localhost:{proxy.port}/live/test-key', str(ca[0]),
                                     str(SECONDS), encoder], capture_output=True, text=True,
                                    timeout=SECONDS + 30)
            wall = time.monotonic() - started
            print(client.stdout, end='', flush=True)
            lines = client.stdout.splitlines()
            done = [l for l in lines if l.startswith('DONE ')]
            assert client.returncode == 0 and done, f'client exit {client.returncode}'
            frames, sent = (int(field.split('=')[1]) for field in done[0].split()[1:3])
            sink_out, _ = sink.communicate(timeout=15)
            print(sink_out, end='')
            received = [l for l in sink_out.splitlines() if l.startswith('SINK BYTES')]
            assert received, 'sink reported nothing'
            got, flv = int(received[0].split()[2]), received[0].split()[4] == '1'
            print(f'{encoder}: {frames} frames in {wall:.1f}s, uploaded {proxy.up} bytes, sink got {got}')
            assert any(l.startswith('LIVE') for l in lines), 'never went LIVE'
            bad = [l for l in lines if l.startswith(('RECONNECTING', 'FAILED'))]
            assert not bad, f'the upload stalled: {bad}'
            assert proxy.accepted == 1, f'{proxy.accepted} TLS connections, expected one'
            assert frames >= SECONDS * 30 // 2, f'only {frames} frames encoded in {SECONDS}s'
            # Nearly all the stream sent arrived (the last packets may still be in flight
            # when the client stops). libx264 pads its
            # 6 Mbit/s CBR, so there a working upload moves megabytes; hardware encoders
            # send the test's flat grey frames in a few bytes.
            assert flv and sent > 0 and got >= sent * 9 // 10, f'sink received {got} of {sent} bytes (FLV header {flv})'
            assert encoder != 'libx264' or got >= 1000000, f'sink received only {got} bytes'
            assert 'test-key' not in client.stdout + client.stderr, 'stream key leaked into logs'
        finally:
            if sink.poll() is None:
                sink.kill()
                sink.wait()
            proxy.close()
        # A certificate from the trusted root, but for another host name.
        wrong_cert, wrong_key = certificate(root, ca, 'wrong.invalid')
        refused(exe, wrong_cert, wrong_key, ca[0], encoder, 'host name mismatch')
        # The right host name from a root the system does not trust: '-' leaves the
        # client on its production trust store (the macOS system anchors, or the
        # distribution's CA bundle on Linux), exactly as a real stream uses it.
        refused(exe, cert, key, '-', encoder, 'untrusted root')
        if sys.platform == 'darwin':
            # Streaming's OpenSSL never reads the keychain: it must be handed every system
            # anchor, including the roots YouTube's ingest chains to.
            roots = subprocess.run([exe, '--system-roots'], capture_output=True, text=True, timeout=30)
            print(roots.stdout, end='')
            assert roots.returncode == 0, 'system trust anchors missing'
        print('PASS: RTMPS upload stayed live through a CA-issued chain; wrong host and untrusted root refused with their reason')
        return 0


if __name__ == '__main__':
    sys.exit(main())
