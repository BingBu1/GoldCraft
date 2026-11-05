#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze the production updater and run actual x86 filesystem/HTTP integration."""
import argparse
import hashlib
import http.server
import json
import os
import pathlib
import shutil
import struct
import subprocess
import threading
import time
import urllib.parse

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
    def freeze(relative):
        source, target = ROOT / relative, out / 'source' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        before = identity(source)
        shutil.copy2(source, target)
        assert identity(source) == before and identity(target)['sha256'] == before['sha256']
        frozen.append(dict(original=before, frozen=identity(target)))
        return target
    for component in ['resource_cache', 'resource_update', 'resource_download', 'resource_overlay']:
        for extension in ['cpp', 'hpp']: freeze('native/client/' + component + '.' + extension)
    freeze('native/include/goldcraft/resource_sync.hpp')
    freeze('tests/resource-update/main.cpp'); freeze('tests/resource-update/run.py')
    sdk = pathlib.Path('external/MetaHookSv-20261007/MetaHook/include')
    for source in (ROOT / sdk).rglob('*'):
        if source.is_file(): freeze(source.relative_to(ROOT))
    fs_dll = freeze('sandbox/cs-client-b/Half-Life/FileSystem_Stdio.dll')
    assert identity(fs_dll)['sha256'] == '07a432b6214c6ea5ea4a988d5c8384d3ab35a531b49fe71c354f3fed5ce9f316'
    def save(name, data): (out / name).write_text(json.dumps(data, ensure_ascii=False, indent=2), 'utf-8')
    save('inputs.json', frozen)
    (out / 'temp').mkdir()
    env = dict(os.environ)
    if args.environment: env.update(json.loads(args.environment.read_text('utf-8-sig')))
    env['TEMP'] = env['TMP'] = str(out / 'temp')
    compiler = str(args.compiler.resolve(strict=True))
    includes = [out / 'source/native/client', out / 'source/native/include']
    includes += [out / 'source' / sdk / x for x in ['', 'Interface', 'HLSDK/common', 'HLSDK/cl_dll', 'HLSDK/engine', 'HLSDK/public', 'HLSDK/pm_shared', 'HLSDK/dlls', 'SourceSDK', 'SourceSDK/tier1']]
    flags = ['/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/MT', '/EHsc', '/O2', '/clang:-O3', '/utf-8',
             '/arch:AVX2', '/fp:precise', '/clang:-flto=thin', '/D_CRT_SECURE_NO_WARNINGS', '/DNOMINMAX']
    flags += ['/I' + str(x) for x in includes]
    steps = []
    def execute(name, command, limit=35):
        start = time.monotonic()
        try:
            p = subprocess.run([str(x) for x in command], cwd=out, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=limit)
            code, data = p.returncode, p.stdout
        except subprocess.TimeoutExpired as e:
            code, data = -999, (e.stdout or b'') + b'\nTEST TIMEOUT\n'
        log = out / (name + '.log'); log.write_bytes(data)
        steps.append(dict(name=name, argv=[str(x) for x in command], exit=code, seconds=time.monotonic()-start, log=identity(log)))
        save('steps.json', steps); print(name, code, flush=True)
        return code
    objects = []
    for name, relative in [(x, 'native/client/' + x + '.cpp') for x in ['resource_cache', 'resource_update', 'resource_download', 'resource_overlay']] + [('fixture', 'tests/resource-update/main.cpp')]:
        obj = out / (name + '.obj'); objects.append(obj)
        assert execute('compile-' + name, [compiler, *flags, '/c', '/Fo' + str(obj), out / 'source' / relative], 120) == 0
    # Avoid Windows legacy installer-name heuristics for an ordinary test host.
    exe = out / 'resource-check.exe'
    assert execute('link', [compiler, '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT', *objects,
                           '/Fe' + str(exe), '/link', '/threads:1', 'winhttp.lib', 'bcrypt.lib']) == 0
    body = bytes(((i * 73 + 19) ^ (i >> 9)) & 255 for i in range(256 * 1024))
    old, third = bytes(x ^ 0x5a for x in body), bytes(x ^ 0xc3 for x in body)
    sha, third_sha = hashlib.sha256(body).hexdigest(), hashlib.sha256(third).hexdigest()
    requests, lock = [], threading.Lock()
    stop = threading.Event()
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'
        def log_message(self, *_): pass
        def do_GET(self):
            parts = urllib.parse.unquote(self.path).strip('/').split('/')
            case = parts[0]
            with lock: requests.append(dict(path=self.path, case=case, time=time.monotonic(), headers=dict(self.headers)))
            content = third if 'third' in parts or 'fresh' in parts else old if case == 'bad-sha' else body
            try:
                self.send_response(200); self.send_header('Content-Length', str(len(content)))
                self.send_header('Connection', 'close'); self.end_headers()
                if case == 'cancel' and 'fresh' not in parts:
                    self.wfile.write(content[:131072]); self.wfile.flush()
                    (out / 'cases' / case / 'http-started').write_text('started', 'ascii')
                    stop.wait(2)
                    self.wfile.write(content[131072:])
                else: self.wfile.write(content)
                self.wfile.flush()
            except (OSError, ConnectionError): pass
            self.close_connection = True
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    address = 'http://127.0.0.1:' + str(server.server_port)
    cases = [('same', 'same', 'models/update.mdl', 0), ('same-pak', 'same', 'sprites/packed.spr', 0),
             ('update', 'update', 'models/update.mdl', 1), ('pak', 'pak', 'sprites/packed.spr', 1),
             ('reuse', 'reuse', 'models/update.mdl', 1), ('same-generation', 'same-generation', 'models/update.mdl', 2),
             ('old-hardlink', 'old-hardlink', 'models/update.mdl', 1), ('cache-hardlink', 'cache-hardlink', 'models/update.mdl', 0),
             ('bad-sha', 'bad-sha', 'models/update.mdl', 3), ('cancel', 'cancel', 'models/update.mdl', 2),
             ('invalid', 'invalid', 'models/update.mdl', 0), ('final-change', 'final-change', 'models/update.mdl', 1),
             ('corrupt-cache', 'update', 'models/update.mdl', 1),
             ('long-name', 'update', 'models/' + 'a' * 52 + '.mdl', 1),
             ('transaction', 'transaction', 'models/update.mdl', 2),
             ('rollback-failure', 'rollback-failure', 'models/update.mdl', 0)]
    results = []
    try:
        for label, mode, name, request_count in cases:
            base = out / 'cases' / label; game = base / 'game'; game.mkdir(parents=True)
            for filename, data in [('new.bin', body), ('old.bin', old), ('third.bin', third), ('old-linked-source.bin', old)]:
                (base / filename).write_bytes(data)
            initial = body if mode == 'same' else old
            if name == 'sprites/packed.spr':
                entry = struct.pack('<56sii', name.encode(), 12, len(initial))
                (game / 'pak0.pak').write_bytes(struct.pack('<4sii', b'PACK', 12 + len(initial), len(entry)) + initial + entry)
            else:
                destination = game / name; destination.parent.mkdir(parents=True)
                if mode == 'old-hardlink': os.link(base / 'old-linked-source.bin', destination)
                else: destination.write_bytes(initial)
            if label == 'corrupt-cache':
                cached = base / 'cache/objects' / sha / name; cached.parent.mkdir(parents=True); cached.write_bytes(old)
            before = [identity(p) for p in game.rglob('*') if p.is_file()] + [identity(base / 'old-linked-source.bin')]
            code = execute('case-' + label, [exe, fs_dll, base, address + '/' + label + '/', mode, name, len(body), sha, third_sha])
            with lock: count = sum(x['case'] == label for x in requests)
            preserved = all(identity(x['path']) == x for x in before)
            temporary = list((base / 'cache').rglob('*.tmp-*')) if (base / 'cache').exists() else []
            result = dict(name=label, mode=mode, exit=code, expectedRequests=request_count, actualRequests=count,
                          originalsPreserved=preserved, temporaryFiles=[str(x) for x in temporary],
                          passed=code == 0 and count == request_count and preserved and not temporary)
            results.append(result); save('cases.json', results)
    finally:
        stop.set(); server.shutdown(); server.server_close(); thread.join()
        save('http-requests.json', requests)
    changed = [x['frozen']['path'] for x in frozen if identity(x['frozen']['path']) != x['frozen']]
    result = dict(success=all(x['passed'] for x in results) and not changed, cases=results, steps=steps,
                  compiler=identity(compiler), executable=identity(exe), frozenChanged=changed,
                  productionModified=False, deployed=False, serverAddress=address,
                  boundary='Real x86 filesystem DLL, production updater/WinHTTP/CNG and final VFS reads; no runtime client, engine model cache or gameplay exercised.')
    save('result.json', result); print(json.dumps(result['cases']))
    raise SystemExit(0 if result['success'] else 1)

if __name__ == '__main__': main()
