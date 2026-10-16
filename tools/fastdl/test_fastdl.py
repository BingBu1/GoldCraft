"""Real loopback HTTP regressions; all fixtures stay in --work (new directory)."""
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
from copy import deepcopy
from http.client import HTTPConnection
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import os
import random
import socket
import subprocess
import sys
import threading
import time
import unittest
from common import resource_name, udp_compatible, base_url, canonical, digest, manifest, plain, child
import publish
import serve
import fetch

WORK = None


@contextmanager
def running(release, handler=None, workers=8, timeout=1):
    server = serve.Server(('127.0.0.1', 0), release, '/cstrike/r1/', workers, timeout)
    if handler:
        server.RequestHandlerClass = handler
    thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': .02})
    thread.start()
    try:
        yield server, f'http://127.0.0.1:{server.server_port}/cstrike/r1/'
    finally:
        server.shutdown()
        server.server_close()
        thread.join(2)
        assert not thread.is_alive() and not server.connections


def request(server, path, method='GET', headers=None):
    client = HTTPConnection('127.0.0.1', server.server_port, timeout=2)
    try:
        client.request(method, path, headers=headers or {})
        response = client.getresponse()
        return response.status, dict(response.getheaders()), response.read()
    finally:
        client.close()


class FastDL(unittest.TestCase):
    def setUp(self):
        self.root = WORK / self._testMethodName
        self.root.mkdir()
        self.source = self.root / "CS 中文 O'Brien" / 'cstrike'
        self.source.mkdir(parents=True)
        self.assets = {
            'maps/probe.bsp': b'GoldCraft-map-fixture\0' * 12000,
            'sound/custom/with space.wav': b'wave' * 20000,
            'models/noise.mdl': random.Random(42).randbytes(80000),
            'events/probe.sc': b'event fixture',
        }
        for name, data in self.assets.items():
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.plan = publish.plan([self.source], self.assets)
        self.output = self.root / 'release'
        self.receipt = publish.publish(self.plan, self.output, 'http://127.0.0.1:8080/cstrike/r1/')
        self.release = serve.Release(self.output)
        self.sha = self.receipt['manifestSha256']
        self.destination = self.root / 'downloads'

    def check_clean(self):
        self.assertFalse((self.destination / '.fastdl.lock').exists())
        self.assertFalse(list(self.destination.glob('.fastdl-*')))

    def test_resource_paths_and_limits(self):
        for name in ('../x.wad', '/maps/x.bsp', 'maps//x.bsp', 'maps/../x.bsp',
                     'maps/x.bsp:secret', 'maps\\x.bsp', 'maps/%2e.bsp', 'maps/.x.bsp',
                     'maps/a.b.bsp', 'maps/CON.bsp', 'sound/LPT1/x.wav', 'maps/x.bsp ',
                     'cfg/server.cfg', 'server.cfg', 'addons/plugin.dll', 'halflife.wad',
                     'maps/~probe.bsp', 'custom_halflife.wad',
                     'maps/中文.bsp', 'maps/x?.bsp', 'maps/' + 'x'*60 + '.bsp'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                resource_name(name)
        self.assertFalse(udp_compatible('events/probe.sc'))
        self.assertTrue(udp_compatible('sound/custom/with space.wav'))
        self.assertEqual(resource_name('custom.wad'), 'custom.wad')

    def test_urls_and_manifest_validation(self):
        self.assertEqual(len(base_url('http://host/' + 'x' * 114 + '/')), 127)
        with self.assertRaises(ValueError):
            base_url('http://host/' + 'x' * 115 + '/')
        for url in ('file:///x/', 'http://user:pw@host/', 'http://host/x?secret=x',
                    'http://host/x', 'http://host/%2e/', 'http://host/../',
                    'http://host";quit;/', 'http://host/;quit/', 'http://host\\name/',
                    'http://host/"/', 'http://host/' + 'x'*120 + '/'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                base_url(url)
        mutations = [None, [], {'schema': True, 'files': []}]
        for key, val in [('bytes', 0), ('bytes', 1 << 24), ('sha256', 'a'), ('udpFallback', 1), ('gzip', [])]:
            value = deepcopy(self.release.value)
            value['files'][0][key] = val
            mutations.append(value)
        for value in mutations:
            with self.subTest(value=value), self.assertRaises(ValueError):
                manifest(value)

    def test_res_and_reslist_capture(self):
        path = self.root / 'map.res'
        path.write_text('// comment\n"sound/custom/with space.wav" // trailing\n maps/probe.bsp\n', encoding='utf-8')
        self.assertEqual(publish.read_res(path), ['sound/custom/with space.wav', 'maps/probe.bsp'])
        path.write_text(' 1. 4 : custom/with space.wav  78.13K  00000000  RES_FATALIFMISSING\n 2. 5 : !sentence 0.00K 00000000 none\n2 Total sound\'s\n')
        self.assertEqual(publish.read_reslist(path, 'sound'), (['sound/custom/with space.wav'], ['!sentence']))
        path.write_text('1. 1 : *1 0.00K 00000000 none\n1 Total model\'s\n')
        self.assertEqual(publish.read_reslist(path, 'model'), ([], ['*1']))

    def test_reject_incomplete_reslist(self):
        row = '1. 1 : maps/probe.bsp 1.00K 00000000 none\n'
        for capture in (row, row + "2 Total generic's\n", row.replace('1. 1', '2. 1') + "1 Total generic's\n",
                        row + "1 Total generic's\n1 Total generic's\n", row.replace('1.00K', 'bad') + "1 Total generic's\n"):
            path = self.root / 'bad.txt'
            path.write_text(capture)
            with self.subTest(capture=capture), self.assertRaises(ValueError):
                publish.read_reslist(path, 'generic')
        path.write_text(''.join(f'{i}. 1 : *{i} 0.00K 00000000 none\n' for i in range(1, 4097)) + "4096 Total model's\n")
        with self.assertRaisesRegex(ValueError, 'capacity'):
            publish.read_reslist(path, 'model')

    def test_roots_precedence_case_collision_and_missing(self):
        other = self.root / 'valve'
        (other / 'maps').mkdir(parents=True)
        (other / 'maps/probe.bsp').write_bytes(b'other')
        value = publish.plan([self.source, other], ['maps/probe.bsp', 'maps/probe.bsp'])
        self.assertEqual(len(value['files']), 1)
        self.assertEqual(value['files'][0]['sha256'], digest(self.source / 'maps/probe.bsp'))
        with self.assertRaisesRegex(ValueError, 'Case collision'):
            publish.plan([self.source], ['maps/probe.bsp', 'Maps/probe.bsp'])
        with self.assertRaises(FileNotFoundError):
            publish.plan([self.source], ['maps/missing.bsp'])

    def test_reparse_point_rejected(self):
        link = self.root / 'linked'
        if os.name == 'nt':
            # Native PowerShell only; link and target are both owned test paths.
            quote = lambda p: "'" + str(p).replace("'", "''") + "'"
            subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
                            'New-Item -ItemType Junction -Path ' + quote(link) + ' -Target ' + quote(self.source) + ' | Out-Null'],
                           check=True, capture_output=True, timeout=10)
        else:
            link.symlink_to(self.source, target_is_directory=True)
        try:
            with self.assertRaises(ValueError):
                plain(link / 'maps/probe.bsp')
            with self.assertRaises(ValueError):
                publish.plan([link], ['maps/probe.bsp'])
        finally:
            if os.name == 'nt':
                link.rmdir()  # Remove junction itself, never its target contents.
            else:
                link.unlink()
        self.assertEqual((self.source / 'maps/probe.bsp').read_bytes(), self.assets['maps/probe.bsp'])

    @unittest.skipUnless(os.name == 'nt', 'Windows extended path normalization')
    def test_windows_extended_path_identity(self):
        extended = Path('\\\\?\\' + str(self.source))
        self.assertEqual(plain(extended), plain(self.source))
        self.assertEqual(child(extended, 'maps/probe.bsp'), child(self.source, 'maps/probe.bsp'))

    def test_publication_deterministic_and_private_sources_excluded(self):
        second = self.root / 'second'
        receipt = publish.publish(self.plan, second, 'http://localhost/cstrike/r1/')
        self.assertEqual(self.sha, receipt['manifestSha256'])
        for path in self.output.rglob('*'):
            if path.is_file() and path.name != 'server-fastdl.cfg':
                self.assertEqual(path.read_bytes(), (second / path.relative_to(self.output)).read_bytes())
        text = (self.output / 'manifest.json').read_text()
        self.assertNotIn('source', text)
        self.assertNotIn(str(self.root), text)
        self.assertIn('sv_allow_dlfile 1', (self.output / 'server-fastdl.cfg').read_text())

    def test_publication_changes_overlap_cancel_and_existing(self):
        with self.assertRaises(FileExistsError):
            publish.publish(self.plan, self.output, 'http://localhost/')
        with self.assertRaisesRegex(ValueError, 'overlaps'):
            publish.publish(self.plan, self.source / 'new', 'http://localhost/')
        cancel = threading.Event()
        cancel.set()
        with self.assertRaises(InterruptedError):
            publish.publish(self.plan, self.root / 'cancel', 'http://localhost/', cancel)
        self.assertFalse(list(self.root.glob('cancel*')))
        (self.source / 'maps/probe.bsp').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Source changed'):
            publish.publish(self.plan, self.root / 'changed', 'http://localhost/')
        self.assertFalse(list(self.root.glob('changed*')))

    @unittest.skipUnless(os.name == 'nt', 'Windows delete-sharing lock')
    def test_atomic_publication_retries_real_windows_lock(self):
        import ctypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p,
                                      ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p]
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        staging, target = self.root / 'held', self.root / 'committed'
        staging.mkdir()
        (staging / 'proof.txt').write_text('complete')
        handle = kernel.CreateFileW(str(staging), 0x80000000, 3, None, 3, 0x02000000, None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
        timer = threading.Timer(.15, lambda: kernel.CloseHandle(handle))
        timer.start()
        start = time.monotonic()
        try:
            publish.commit_release(staging, target)
        finally:
            timer.join(1)
        self.assertGreater(time.monotonic() - start, .1)
        self.assertLess(time.monotonic() - start, 2)
        self.assertEqual((target / 'proof.txt').read_text(), 'complete')
        self.assertFalse(staging.exists())

    def test_http_raw_head_range_and_etag(self):
        with running(self.release) as (server, _):
            path = '/cstrike/r1/maps/probe.bsp'
            status, headers, data = request(server, path, headers={'Accept-Encoding': 'gzip'})
            self.assertEqual((status, data), (200, self.assets['maps/probe.bsp']))
            self.assertNotIn('Content-Encoding', headers)
            self.assertEqual(request(server, path, 'HEAD')[2], b'')
            self.assertEqual(request(server, path, headers={'If-None-Match': headers['ETag']})[0], 304)
            for value, expected in [('bytes=2-8', data[2:9]), ('bytes=-7', data[-7:]), ('bytes=250000-', data[250000:])]:
                status, _, part = request(server, path, headers={'Range': value})
                self.assertEqual((status, part), (206, expected))
            self.assertEqual(request(server, path, headers={'Range': 'bytes=2-8', 'If-Range': 'stale'})[2], data)
            for invalid in ('bytes=999999-', 'bytes=-0', 'bytes=8-2', 'bytes=1-2,4-5', 'bytes=-'):
                self.assertEqual(request(server, path, headers={'Range': invalid})[0], 416)
            self.assertEqual(request(server, '/cstrike/r1/sound/custom/with%20space.wav')[2], self.assets['sound/custom/with space.wav'])

    def test_http_10_compatibility_and_exposure(self):
        with running(self.release) as (server, _):
            with socket.create_connection(server.server_address, timeout=2) as sock:
                sock.sendall(b'GET /cstrike/r1/events/probe.sc HTTP/1.0\r\n\r\n')
                chunks = []
                while data := sock.recv(4096):
                    chunks.append(data)
            header, body = b''.join(chunks).split(b'\r\n\r\n', 1)
            self.assertIn(b'200 OK', header)
            self.assertEqual(body, self.assets['events/probe.sc'])
            for path in ('', 'server-fastdl.cfg', '../server.cfg', '%2e%2e/server.cfg',
                         'maps%2fprobe.bsp', 'maps%5cprobe.bsp', 'maps/%252e%252e/x.bsp',
                         'maps/probe.bsp?x=1', 'maps/probe.bsp%00', 'models/', 'manifest.json.bak'):
                with self.subTest(path=path):
                    self.assertEqual(request(server, '/cstrike/r1/' + path)[0], 404)
            self.assertEqual(request(server, '/wrong/manifest.json')[0], 404)

    def test_modified_release_rejected(self):
        path = self.output / 'maps/probe.bsp'
        path.write_bytes(b'bad')
        with self.assertRaises(ValueError):
            serve.Release(self.output)
        with running(self.release) as (server, _):
            self.assertEqual(request(server, '/cstrike/r1/maps/probe.bsp')[0], 503)

    def test_worker_bound_overload_and_shutdown(self):
        with running(self.release, workers=2, timeout=.25) as (server, _):
            sockets = [socket.create_connection(server.server_address, timeout=2) for _ in range(2)]
            try:
                until = time.monotonic() + 2
                while server.peak_connections != 2 and time.monotonic() < until:
                    time.sleep(.01)
                self.assertEqual(server.peak_connections, 2)
                self.assertEqual(request(server, '/cstrike/r1/manifest.json')[0], 503)
                start = time.monotonic()
                server.shutdown()
                server.server_close()  # Actively closes stalled request headers.
                self.assertLess(time.monotonic() - start, 1)
                self.assertFalse(server.connections)
            finally:
                for sock in sockets:
                    sock.close()

    def test_fetch_concurrent_compressed_raw_and_cached(self):
        class Delayed(serve.Handler):
            def do_GET(self):
                if not self.path.endswith('manifest.json'):
                    time.sleep(.08)
                super().do_GET()
        with running(self.release, Delayed) as (server, url):
            result = fetch.fetch(url, self.sha, self.destination, workers=4)
            self.assertTrue(result['passed'], result)
            self.assertGreaterEqual(server.peak_connections, 2)
            self.assertLessEqual(server.peak_connections, 4)
            self.assertLess(result['wireBytes'], sum(map(len, self.assets.values())))
            self.assertTrue(any(r['gzip'] for r in result['files']))
            self.assertTrue(any(not r['gzip'] for r in result['files']))
            self.assertFalse(any(r['rawFallback'] for r in result['files']), result)
            for name, data in self.assets.items():
                self.assertEqual((self.destination / name).read_bytes(), data)
                self.assertEqual((self.destination / name).stat().st_nlink, 1)
            cached = fetch.fetch(url, self.sha, self.destination)
            self.assertEqual({r['state'] for r in cached['files']}, {'cached'})
            self.assertEqual(cached['wireBytes'], 0)
        self.check_clean()

    def test_gzip_http_failure_uses_raw(self):
        class MissingGzip(serve.Handler):
            def do_GET(self):
                if '/_fastdl/' in self.path:
                    self.send_error(404)
                else:
                    super().do_GET()
        with running(self.release, MissingGzip) as (_, url):
            result = fetch.fetch(url, self.sha, self.destination)
        self.assertTrue(result['passed'], result)
        self.assertTrue(any(r['rawFallback'] for r in result['files']))
        self.check_clean()

    def test_failed_download_leaves_native_fallback(self):
        class MissingAssets(serve.Handler):
            def do_GET(self):
                if self.path.endswith('manifest.json'):
                    super().do_GET()
                else:
                    self.send_error(404)
        with running(self.release, MissingAssets) as (_, url):
            result = fetch.fetch(url, self.sha, self.destination)
        self.assertFalse(result['passed'])
        self.assertEqual({r['state'] for r in result['files']}, {'fallback'})
        self.assertFalse(any(self.destination.rglob('*')))
        self.check_clean()

    def test_conflicting_file_and_lock_preserved(self):
        existing = self.destination / 'maps/probe.bsp'
        existing.parent.mkdir(parents=True)
        existing.write_bytes(b'user existing file')
        with running(self.release) as (_, url):
            result = fetch.fetch(url, self.sha, self.destination)
            self.assertFalse(result['passed'])
            self.assertEqual(existing.read_bytes(), b'user existing file')
            self.check_clean()
            lock = self.destination / '.fastdl.lock'
            lock.write_text('other process')
            with self.assertRaises(FileExistsError):
                fetch.fetch(url, self.sha, self.destination)
            self.assertEqual(lock.read_text(), 'other process')

    def test_trusted_manifest_required_and_redirect_rejected(self):
        with running(self.release) as (_, url):
            with self.assertRaisesRegex(ValueError, 'integrity'):
                fetch.fetch(url, '0' * 64, self.destination)
        self.assertFalse(self.destination.exists())
        class Redirect(serve.Handler):
            def do_GET(self):
                self.send_response(302)
                self.send_header('Location', 'http://127.0.0.1:1/unused')
                self.send_header('Content-Length', '0')
                self.end_headers()
        with running(self.release, Redirect) as (_, url):
            with self.assertRaisesRegex(ValueError, 'Redirect'):
                fetch.fetch(url, self.sha, self.destination)
        self.assertFalse(self.destination.exists())

    def test_corruption_truncation_and_length_rejected(self):
        for mode in ('corrupt', 'truncated', 'length'):
            class Broken(serve.Handler):
                def do_GET(self):
                    if self.path.endswith('manifest.json'):
                        return super().do_GET()
                    name = self.path.removeprefix(self.server.prefix).replace('%20', ' ')
                    original = (self.server.release.root / name).read_bytes()
                    data = b'!' + original[1:] if mode == 'corrupt' else original[:-1]
                    self.send_response(200)
                    self.send_header('Content-Length', str(len(original) + (1 if mode == 'length' else 0)))
                    self.send_header('Connection', 'close')
                    self.end_headers()
                    self.wfile.write(data)
                    self.close_connection = True
            with running(self.release, Broken) as (_, url):
                result = fetch.fetch(url, self.sha, self.destination)
            with self.subTest(mode=mode):
                self.assertFalse(result['passed'])
                self.assertFalse(any(self.destination.rglob('*')))
            self.check_clean()

    def test_gzip_bomb_and_trailing_stream_rejected(self):
        row = next(r for r in self.release.value['files'] if r['path'] == 'maps/probe.bsp')
        original = (self.output / row['gzip']['path']).read_bytes()
        for i, data in enumerate((gzip.compress(b'x' * (row['bytes'] + 1)), original + gzip.compress(b'extra'))):
            class Encoded(serve.Handler):
                def do_GET(self):
                    self.send_response(200)
                    self.send_header('Content-Length', str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
            variant = dict(path='bad.gz', bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
            with running(self.release, Encoded) as (_, url), self.assertRaises(ValueError):
                fetch.transfer(url, variant, row, self.root / f'bomb-{i}.tmp', threading.Event(), 1, time.monotonic() + 2)

    def test_cancellation_stalled_socket_cleans_partial(self):
        started, release = threading.Event(), threading.Event()
        class Stalled(serve.Handler):
            def do_GET(self):
                if self.path.endswith('manifest.json'):
                    return super().do_GET()
                self.send_response(200)
                name = self.path.removeprefix(self.server.prefix).replace('%20', ' ')
                self.send_header('Content-Length', str(self.server.release.files[name]['bytes']))
                self.end_headers()
                started.set()
                release.wait(2)
        cancel = threading.Event()
        with running(self.release, Stalled) as (_, url), ThreadPoolExecutor(1) as pool:
            job = pool.submit(fetch.fetch, url, self.sha, self.destination, 2, .2, 1, cancel)
            self.assertTrue(started.wait(2))
            before = time.monotonic()
            cancel.set()
            result = job.result(2)
            release.set()
            self.assertLess(time.monotonic() - before, 1)
        self.assertEqual({r['state'] for r in result['files']}, {'cancelled'})
        self.assertFalse(any(self.destination.rglob('*')))
        self.check_clean()


def main():
    global WORK
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    WORK = plain(args.work)
    WORK.mkdir(parents=True, exist_ok=False)
    if os.name == 'nt':
        import ctypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.GetCurrentProcess.restype = ctypes.c_void_p
        kernel.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint]
        if not kernel.SetPriorityClass(kernel.GetCurrentProcess(), 0x4000):
            raise ctypes.WinError(ctypes.get_last_error())
    start = time.monotonic()
    with (WORK / 'tests.log').open('w', encoding='utf-8') as stream:
        result = unittest.TextTestRunner(stream=stream, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(FastDL))
    report = dict(tests=result.testsRun, failures=len(result.failures), errors=len(result.errors), skipped=len(result.skipped),
                  seconds=time.monotonic() - start, passed=result.wasSuccessful(),
                  sources={p.name: digest(p) for p in Path(__file__).parent.glob('*.py')})
    (WORK / 'result.json').write_bytes(canonical(report))
    print((WORK / 'tests.log').read_text())
    print(json.dumps(report))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
