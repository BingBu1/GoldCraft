"""Optional verified concurrent prefetch, before starting CS.

Ordinary CS already uses FastDL without this tool. Failures leave files missing
for the normal engine HTTP/UDP path; this tool never auto-connects or launches CS.
The manifest SHA must come from the server operator's trusted release receipt.
"""
from concurrent.futures import ThreadPoolExecutor, as_completed
from http.client import HTTPException
from pathlib import Path
from urllib.request import Request, build_opener, ProxyHandler, HTTPRedirectHandler
from urllib.parse import quote
import argparse
import hashlib
import json
import os
import shutil
import tempfile
import threading
import time
import zlib
from common import (CHUNK, MANIFEST_LIMIT, plain, child, digest, manifest, base_url,
                    canonical)


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *_):
        raise ValueError('Redirect rejected; use the published final origin')


def connection(url, timeout):
    # Direct connections are local to this opener; never changes system proxy.
    opener = build_opener(ProxyHandler({}), NoRedirect())
    return opener.open(Request(url, headers={'User-Agent': 'GoldCraftFastDL/1', 'Accept-Encoding': 'identity'}), timeout=timeout)


def get_manifest(url, expected_sha, timeout, cancel):
    if not isinstance(expected_sha, str) or len(expected_sha) != 64 or any(c not in '0123456789abcdef' for c in expected_sha):
        raise ValueError('Trusted manifest SHA256 is required')
    with connection(base_url(url) + 'manifest.json', timeout) as response:
        if response.status != 200 or response.headers.get('Content-Encoding', 'identity') != 'identity':
            raise ValueError('Unexpected manifest response')
        chunks, total, deadline = [], 0, time.monotonic() + 30
        while True:
            if cancel.is_set():
                raise InterruptedError('Cancelled')
            if time.monotonic() > deadline:
                raise TimeoutError('Manifest deadline exceeded')
            chunk = response.read1(min(CHUNK, MANIFEST_LIMIT - total + 1))
            if not chunk:
                break
            chunks.append(chunk)
            total += len(chunk)
            if total > MANIFEST_LIMIT:
                raise ValueError('Manifest too large')
        data = b''.join(chunks)
    if len(data) > MANIFEST_LIMIT or hashlib.sha256(data).hexdigest() != expected_sha:
        raise ValueError('Manifest integrity mismatch')
    value = manifest(json.loads(data))
    if data != canonical(value):
        raise ValueError('Noncanonical manifest')
    return value


def transfer(url, variant, row, output, cancel, timeout, deadline, traffic=None):
    if cancel.is_set():
        raise InterruptedError('Cancelled')
    raw_hash, encoded_hash = hashlib.sha256(), hashlib.sha256()
    written = received = 0
    decoder = zlib.decompressobj(16 + zlib.MAX_WBITS) if variant is not row else None
    with connection(url + quote(variant['path'], safe='/'), timeout) as response, output.open('xb') as dest:
        if response.status != 200 or response.headers.get('Content-Encoding', 'identity') != 'identity':
            raise ValueError('Unexpected asset response')
        if response.headers.get('Content-Length') != str(variant['bytes']):
            raise ValueError('Asset length mismatch')
        while True:
            if cancel.is_set():
                raise InterruptedError('Cancelled')
            if time.monotonic() > deadline:
                raise TimeoutError('Asset deadline exceeded')
            data = response.read1(min(CHUNK, variant['bytes'] - received + 1))
            if not data:
                break
            received += len(data)
            if traffic is not None:
                traffic[0] += len(data)
            if received > variant['bytes']:
                raise ValueError('Oversized response')
            encoded_hash.update(data)
            decoded = decoder.decompress(data, row['bytes'] - written + 1) if decoder else data
            written += len(decoded)
            if written > row['bytes'] or (decoder and (decoder.unconsumed_tail or decoder.unused_data)):
                raise ValueError('Invalid or oversized compressed resource')
            raw_hash.update(decoded)
            dest.write(decoded)
        if decoder and (not decoder.eof or decoder.unused_data or decoder.unconsumed_tail):
            raise ValueError('Truncated/trailing gzip stream')
        if received != variant['bytes'] or encoded_hash.hexdigest() != variant['sha256']:
            raise ValueError('Transfer integrity mismatch')
        if written != row['bytes'] or raw_hash.hexdigest() != row['sha256']:
            raise ValueError('Decoded integrity mismatch')
        dest.flush()
        os.fsync(dest.fileno())
    return received


def fetch(url, expected_sha, destination, workers=4, timeout=3, file_deadline=60, cancel=None):
    if not 1 <= workers <= 8 or not 0.1 <= timeout <= 15 or not timeout <= file_deadline <= 300:
        raise ValueError('Invalid worker/timeout budget')
    url = base_url(url)
    cancel = cancel or threading.Event()
    if cancel.is_set():
        raise InterruptedError('Cancelled')
    value = get_manifest(url, expected_sha, timeout, cancel)
    root = plain(destination)
    # Validate all destinations before any network data can write to them.
    for row in value['files']:
        child(root, row['path'])
    root.mkdir(parents=True, exist_ok=True)
    lock = child(root, '.fastdl.lock')
    lock_fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    staging = None
    started = time.monotonic()
    try:
        os.close(lock_fd)
        staging = Path(tempfile.mkdtemp(prefix='.fastdl-', dir=root))

        def one(index, row):
            if cancel.is_set():
                return dict(path=row['path'], state='cancelled', wireBytes=0)
            target = child(root, row['path'])
            if target.exists():
                if target.is_file() and target.stat().st_size == row['bytes'] and digest(target) == row['sha256']:
                    return dict(path=row['path'], state='cached', wireBytes=0)
                return dict(path=row['path'], state='conflict', error='Existing different file preserved', wireBytes=0)
            temp = staging / str(index)
            traffic = [0]
            variants = ([row['gzip']] if 'gzip' in row else []) + [row]
            fallback = False
            fallback_error = None
            for variant in variants:
                try:
                    transfer(url, variant, row, temp, cancel, timeout, time.monotonic() + file_deadline, traffic)
                    if cancel.is_set():
                        raise InterruptedError('Cancelled')
                    target = child(root, row['path'])
                    target.parent.mkdir(parents=True, exist_ok=True)
                    # Atomic no-clobber publication of this verified temporary
                    # inode. The temporary link is immediately removed; nothing
                    # is linked to any original game file or input installation.
                    os.link(temp, target)
                    temp.unlink()
                    return dict(path=row['path'], state='downloaded', wireBytes=traffic[0],
                                gzip=variant is not row, rawFallback=fallback, rawFallbackError=fallback_error)
                except (OSError, ValueError, zlib.error, HTTPException) as error:
                    if temp.exists():
                        temp.unlink()
                    if cancel.is_set():
                        return dict(path=row['path'], state='cancelled', wireBytes=traffic[0])
                    if variant is not row:
                        fallback = True
                        fallback_error = str(error)
                        continue
                    return dict(path=row['path'], state='fallback', error=type(error).__name__, wireBytes=traffic[0])

        results = []
        with ThreadPoolExecutor(max_workers=workers, thread_name_prefix='fastdl') as pool:
            jobs = [pool.submit(one, i, row) for i, row in enumerate(value['files'])]
            try:
                for job in as_completed(jobs):
                    results.append(job.result())
            except BaseException:
                cancel.set()
                for job in jobs:
                    job.cancel()
                raise
        results.sort(key=lambda x: x['path'])
        return dict(passed=all(x['state'] in ('downloaded', 'cached') for x in results),
                    seconds=time.monotonic() - started, files=results,
                    wireBytes=sum(x['wireBytes'] for x in results),
                    fallback=[x['path'] for x in results if x['state'] not in ('downloaded', 'cached')])
    finally:
        if staging and staging.exists():
            checked = plain(staging)
            if checked.parent != root:
                raise ValueError('Unsafe staging cleanup')
            shutil.rmtree(checked)
        lock.unlink()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--url', required=True)
    p.add_argument('--manifest-sha256', required=True)
    p.add_argument('--destination', required=True, type=Path, help='cstrike_downloads resource directory')
    p.add_argument('--workers', type=int, default=4)
    p.add_argument('--timeout', type=float, default=3)
    p.add_argument('--deadline', type=float, default=60)
    args = p.parse_args()
    try:
        result = fetch(args.url, args.manifest_sha256, args.destination, args.workers, args.timeout, args.deadline)
    except KeyboardInterrupt:
        print(json.dumps(dict(passed=False, cancelled=True)))
        raise SystemExit(130)
    print(json.dumps(result))
    raise SystemExit(0 if result['passed'] else 2)


if __name__ == '__main__':
    main()
