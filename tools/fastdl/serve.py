"""Bounded HTTP FastDL server. Serves only the verified release manifest.

Ordinary GoldSrc requests receive raw bytes, without redirects or compression
negotiation. The optional verified prefetcher uses explicit gzip blob URLs.
"""
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn
from pathlib import Path
from urllib.parse import unquote, urlsplit
import argparse
import hashlib
import json
import os
import re
import socket
import threading
from common import CHUNK, MANIFEST_LIMIT, plain, child, manifest, canonical


def fingerprint(s):
    return (s.st_size, s.st_mtime_ns, s.st_ctime_ns, s.st_ino)


class Release:
    def __init__(self, root):
        self.root = plain(root)
        path = child(self.root, 'manifest.json')
        if path.stat().st_size > MANIFEST_LIMIT:
            raise ValueError('Manifest too large')
        data = path.read_bytes()
        self.value = manifest(json.loads(data))
        if data != canonical(self.value):
            raise ValueError('Noncanonical manifest')
        self.files = {'manifest.json': dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())}
        for row in self.value['files']:
            self.files[row['path']] = row
            if 'gzip' in row:
                self.files[row['gzip']['path']] = row['gzip']
        self.identities = {}
        for name, row in self.files.items():
            path = child(self.root, name)
            # Compare handle metadata to handle metadata. On Windows/Python
            # 3.14 stat(path).st_ctime and fstat(fd).st_ctime can differ.
            with path.open('rb') as f:
                before = os.fstat(f.fileno())
                if before.st_size != row['bytes'] or hashlib.file_digest(f, 'sha256').hexdigest() != row['sha256'] or fingerprint(before) != fingerprint(os.fstat(f.fileno())):
                    raise ValueError('Release changed or corrupt: ' + name)
            self.identities[name] = fingerprint(before)


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    server_version = 'GoldCraftFastDL/1'
    sys_version = ''

    def setup(self):
        self.request.settimeout(self.server.io_timeout)
        super().setup()

    def log_message(self, *_):
        pass  # No unbounded access logs or untrusted path interpolation.

    def do_HEAD(self):
        self.serve(head=True)

    def do_GET(self):
        self.serve(head=False)

    def serve(self, head):
        self.close_connection = True  # A slow keepalive cannot own a worker.
        release = self.server.release
        try:
            url = urlsplit(self.path)
            if url.scheme or url.netloc or url.query or url.fragment or not url.path.startswith(self.server.prefix):
                self.send_error(404)
                return
            encoded = url.path[len(self.server.prefix):]
            if re.search(r'%(?:2f|5c|00)', encoded, re.I):
                self.send_error(404)
                return
            name = unquote(encoded, errors='strict')
            row = release.files.get(name)
            if row is None:
                self.send_error(404)
                return
            path = child(release.root, name)
            with path.open('rb') as f:
                if fingerprint(os.fstat(f.fileno())) != release.identities[name]:
                    self.send_error(503, 'Release changed; publish a new directory')
                    return
                etag = '"' + row['sha256'] + '"'
                if self.headers.get('If-None-Match') == etag:
                    self.send_response(304)
                    self.send_header('ETag', etag)
                    self.send_header('Content-Length', '0')
                    self.send_header('Connection', 'close')
                    self.end_headers()
                    return
                start, end, status = 0, row['bytes'] - 1, 200
                value = self.headers.get('Range')
                if value and self.headers.get('If-Range', etag) == etag:
                    match = re.fullmatch(r'bytes=(\d*)-(\d*)', value)
                    if not match or not (match[1] or match[2]):
                        self.send_error(416)
                        return
                    if match[1]:
                        start = int(match[1])
                        end = min(end, int(match[2])) if match[2] else end
                    else:
                        length = int(match[2])
                        if length == 0:
                            self.send_error(416)
                            return
                        start = max(0, row['bytes'] - length)
                    if start > end or start >= row['bytes']:
                        self.send_response(416)
                        self.send_header('Content-Range', f"bytes */{row['bytes']}")
                        self.send_header('Content-Length', '0')
                        self.send_header('Connection', 'close')
                        self.end_headers()
                        return
                    status = 206
                self.send_response(status)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Length', str(end - start + 1))
                self.send_header('Accept-Ranges', 'bytes')
                self.send_header('ETag', etag)
                self.send_header('Cache-Control', 'public, max-age=0, must-revalidate')
                self.send_header('X-Content-Type-Options', 'nosniff')
                self.send_header('Connection', 'close')
                if status == 206:
                    self.send_header('Content-Range', f"bytes {start}-{end}/{row['bytes']}")
                self.end_headers()
                if head:
                    return
                f.seek(start)
                remaining = end - start + 1
                while remaining:
                    if self.server.stopping.is_set():
                        break
                    data = f.read(min(CHUNK, remaining))
                    if not data:
                        break
                    self.wfile.write(data)
                    remaining -= len(data)
        except (OSError, UnicodeError, ValueError):
            # Disconnection, timeout or cancelled transfer: close the response.
            self.close_connection = True


class Server(ThreadingMixIn, HTTPServer):
    daemon_threads = False
    block_on_close = True
    allow_reuse_address = False
    request_queue_size = 32

    def __init__(self, address, release, prefix='/', workers=8, io_timeout=5):
        if not 1 <= workers <= 32 or not 0.1 <= io_timeout <= 30:
            raise ValueError('workers 1..32 and timeout 0.1..30 seconds required')
        if not re.fullmatch(r'/(?:[A-Za-z0-9_-]+/)*', prefix):
            raise ValueError('Use an absolute plain URL prefix ending /')
        self.release, self.prefix, self.io_timeout = release, prefix, io_timeout
        self.slots = threading.BoundedSemaphore(workers)
        self.stopping = threading.Event()
        self.lock = threading.Lock()
        self.connections = set()
        self.peak_connections = 0
        super().__init__(address, Handler)

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            try:
                # Drain one bounded header packet before rejecting. Closing an
                # unread GET on Windows can reset the connection and discard
                # the 503 response. Never let an overloaded peer block accept.
                request.settimeout(0.02)
                try:
                    request.recv(4096)
                except TimeoutError:
                    pass
                request.settimeout(0.1)
                request.sendall(b'HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\nRetry-After: 1\r\n\r\n')
            except OSError:
                pass
            self.shutdown_request(request)
            return
        with self.lock:
            self.connections.add(request)
            self.peak_connections = max(self.peak_connections, len(self.connections))
        try:
            super().process_request(request, address)
        except BaseException:
            with self.lock:
                self.connections.discard(request)
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            with self.lock:
                self.connections.discard(request)
            self.slots.release()

    def handle_error(self, *_):
        pass

    def server_close(self):
        self.stopping.set()
        with self.lock:
            for connection in self.connections:
                try:
                    connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        super().server_close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--release', required=True, type=Path)
    p.add_argument('--bind', default='127.0.0.1')
    p.add_argument('--port', type=int, default=8080)
    p.add_argument('--prefix', default='/')
    p.add_argument('--workers', type=int, default=8)
    p.add_argument('--timeout', type=float, default=5)
    args = p.parse_args()
    with Server((args.bind, args.port), Release(args.release), args.prefix, args.workers, args.timeout) as server:
        print(json.dumps(dict(address=server.server_address, prefix=args.prefix)), flush=True)
        try:
            server.serve_forever(poll_interval=0.1)
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()
