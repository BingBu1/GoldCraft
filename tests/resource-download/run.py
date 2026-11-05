#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze and test the production x86 downloader against controlled loopback HTTP."""
import argparse
import hashlib
import http.server
import json
import os
import pathlib
import shutil
import subprocess
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]

def identity(path):
    path = pathlib.Path(path).resolve()
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--compiler', type=pathlib.Path, required=True)
    parser.add_argument('--environment', type=pathlib.Path,
                        help='Optional JSON environment from the configured Windows toolchain')
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    out.mkdir(parents=True)
    frozen = []
    for relative in ['native/client/resource_download.hpp', 'native/client/resource_download.cpp',
                     'native/include/goldcraft/resource_sync.hpp', 'tests/resource-download/main.cpp',
                     'tests/resource-download/run.py']:
        source, target = ROOT / relative, out / 'source' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        before = identity(source)
        shutil.copy2(source, target)
        assert identity(source) == before and identity(target)['sha256'] == before['sha256']
        frozen.append(dict(original=before, frozen=identity(target)))
    def save(name, value): (out / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), 'utf-8')
    save('inputs.json', frozen)
    (out / 'temp').mkdir()
    env = dict(os.environ)
    if args.environment: env.update(json.loads(args.environment.read_text('utf-8-sig')))
    env['TEMP'] = env['TMP'] = str(out / 'temp')
    compiler = str(args.compiler.resolve(strict=True))
    flags = ['/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/MT', '/EHsc', '/O2', '/utf-8',
             '/arch:AVX2', '/fp:precise', '/I' + str(out / 'source/native/client'), '/I' + str(out / 'source/native/include')]
    steps = []
    def execute(name, command, limit=30):
        start = time.monotonic()
        try:
            p = subprocess.run([str(a) for a in command], cwd=out, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=limit)
            code, data = p.returncode, p.stdout
        except subprocess.TimeoutExpired as e:
            code, data = -999, (e.stdout or b'') + b'\nTEST PROCESS TIMEOUT\n'
        path = out / (name + '.log'); path.write_bytes(data)
        steps.append(dict(name=name, argv=[str(a) for a in command], exit=code, seconds=time.monotonic() - start, log=identity(path)))
        save('steps.json', steps); print(name, code, flush=True)
        return code
    for label, relative in [('worker', 'native/client/resource_download.cpp'), ('fixture', 'tests/resource-download/main.cpp')]:
        assert execute('compile-' + label, [compiler, *flags, '/c', '/Fo' + str(out / (label + '.obj')),
                                           out / 'source' / relative], 120) == 0
    exe = out / 'download.exe'
    assert execute('link', [compiler, '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT', out / 'worker.obj',
                           out / 'fixture.obj', '/Fe' + str(exe), '/link', '/threads:1', 'winhttp.lib', 'bcrypt.lib']) == 0
    # Independent Python source bytes and hashlib establish the HTTP/manifest oracle.
    body = bytes(((i * 73 + 19) ^ (i >> 9)) & 255 for i in range(256 * 1024))
    old = bytes((x ^ 0x5a) for x in body)
    (out / 'new-body.bin').write_bytes(body); (out / 'old-body.bin').write_bytes(old)
    digest, old_digest = hashlib.sha256(body).hexdigest(), hashlib.sha256(old).hexdigest()
    requests, lock = [], threading.Lock()
    stop = threading.Event()
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'
        def log_message(self, *_): pass
        def do_GET(self):
            with lock:
                count = sum(x['path'] == self.path for x in requests) + 1
                requests.append(dict(path=self.path, request=count, time=time.monotonic(), headers=dict(self.headers)))
            path = self.path
            status, content, length = 200, body, len(body)
            if path.startswith('/old'): content = old
            elif path.startswith('/wrong'): content = b'error html' + b' ' * (len(body) - 10)
            elif path.startswith('/404'): status, content, length = 404, b'', 0
            elif path.startswith('/redirect'): status, content, length = 302, b'', 0
            elif path.startswith('/flaky') and count < 3: status, content, length = 503, b'', 0
            elif path.startswith('/length'): length -= 1
            elif path.startswith('/trunc'): content = body[:8192]
            try:
                self.send_response(status)
                self.send_header('Content-Length', str(length))
                self.send_header('Connection', 'close')
                if status == 302: self.send_header('Location', '/redirect-target')
                self.end_headers()
                if path.startswith('/slow'):
                    self.wfile.write(content[:131072]); self.wfile.flush()
                    stop.wait(3)
                    self.wfile.write(content[131072:])
                else:
                    self.wfile.write(content)
                self.wfile.flush()
            except (OSError, ConnectionError): pass
            self.close_connection = True
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    base = 'http://127.0.0.1:' + str(server.server_port)
    cases = [('same-length-new', 'good', '/good', digest, 1),
             ('old-body', 'bad', '/old', digest, 3),
             ('old-sha', 'bad', '/good-old-sha', old_digest, 3),
             ('truncated', 'bad', '/trunc', digest, 3),
             ('length-mismatch', 'bad', '/length', digest, 3),
             ('wrong-200', 'bad', '/wrong', digest, 3),
             ('not-found', 'bad', '/404', digest, 3),
             ('no-redirect', 'bad', '/redirect', digest, 3),
             ('retry', 'good', '/flaky', digest, 3),
             ('completed-job-reuse', 'reuse', '/reuse', digest, 1),
             ('cancel', 'cancel', '/slow-cancel', digest, 1),
             ('pre-cancel', 'pre-cancel', '/never', digest, 0),
             ('shutdown-lifetime', 'lifetime', '/slow-lifetime', digest, 32),
             ('destructor', 'destructor', '/slow-destructor', digest, 1)]
    results = []
    try:
        for name, mode, path, sha, expected_requests in cases:
            code = execute(name, [exe, mode, base + path, len(body), sha, out / 'new-body.bin'], 75 if mode == 'lifetime' else 25)
            with lock:
                matched = [x for x in requests if x['path'].startswith(path)]
                redirects = [x for x in requests if x['path'] == '/redirect-target']
            passed = code == 0 and len(matched) == expected_requests and not redirects
            results.append(dict(name=name, exit=code, expectedRequests=expected_requests, actualRequests=len(matched), passed=passed))
            save('cases.json', results)
    finally:
        stop.set(); server.shutdown(); server.server_close(); thread.join()
        save('http-requests.json', requests)
    changed = [x['frozen']['path'] for x in frozen if identity(x['frozen']['path']) != x['frozen']]
    result = dict(success=all(x['passed'] for x in results) and not changed, cases=results, steps=steps,
                  frozen=frozen, compiler=identity(compiler), bodies=[identity(out / 'new-body.bin'), identity(out / 'old-body.bin')],
                  changed=changed, serverAddress=base, productionModified=False, deployed=False,
                  boundary='Actual x86 C++20 WinHTTP+BCrypt module, real loopback HTTP bytes; engine integration and real game not exercised.')
    save('result.json', result)
    print(json.dumps(dict(success=result['success'], cases=results)))
    raise SystemExit(0 if result['success'] else 1)

if __name__ == '__main__': main()
