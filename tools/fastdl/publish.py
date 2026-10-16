"""Build a reviewed, immutable FastDL release from explicit engine resources.

Sources are read-only. No server commands, config deployment or public listener.
"""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import os
import re
import shutil
import tempfile
import time
from common import (SCHEMA, MAX_FILE, MAX_FILES, CHUNK, resource_name, plain, child,
                    digest, canonical, manifest, base_url, udp_compatible)


def read_res(path):
    result = []
    for line in plain(path).read_text('utf-8-sig').splitlines():
        # Matches the pinned REHLDS_FIXES SV_TrimResourceLine boundary rule.
        line = re.split(r'(?<!\S)//', line, maxsplit=1)[0].strip()
        if len(line) >= 2 and line[0] == line[-1] == '"':
            line = line[1:-1]
        if line:
            result.append(resource_name(line))
    return result


def read_reslist(path, kind):
    """Consume one complete `reslist KIND` capture; reject truncated output.

    The displayed two-decimal KiB size and truncated MD5 are not integrity data.
    File sizes/hashes are computed from the actual deployment below.
    """
    rows, count, skipped = [], None, []
    for line in plain(path).read_text('utf-8-sig').splitlines():
        item = re.fullmatch(r'\s*(\d+)\.\s+\d+\s+: (.*?)\s+-?\d+\.\d{2}K\s+[0-9a-fA-F]{8}\s+.*', line)
        end = re.fullmatch(r"\s*(\d+) Total " + kind + r"'s\s*", line)
        if item:
            if count is not None or int(item[1]) != len(rows) + len(skipped) + 1:
                raise ValueError('Out-of-order/combined reslist capture')
            name = item[2].rstrip()
            if name.startswith(('*', '!')):
                skipped.append(name)  # embedded BSP submodel / sentence, no file
                continue
            rows.append(resource_name('sound/' + name if kind == 'sound' else name))
        elif end:
            if count is not None:
                raise ValueError('Multiple reslist trailers')
            count = int(end[1])
        elif re.match(r'\s*\d+\.\s+\d+\s+:', line):
            raise ValueError('Malformed reslist row')
    if count is None or count != len(rows) + len(skipped):
        raise ValueError('Missing/truncated reslist trailer')
    # This engine command uses a fixed 4096 entry temporary array even though
    # GoldCraft can exceed it. A full array is ambiguous; use an explicit list.
    if count >= 4096:
        raise ValueError('reslist reached engine capture capacity; use complete explicit resource list')
    return rows, skipped


def plan(roots, names):
    roots = [plain(p) for p in roots]
    if not roots or any(not p.is_dir() for p in roots):
        raise ValueError('Supply existing ordered game/downloads/valve roots')
    seen, rows = {}, []
    for name in names:
        resource_name(name)
        key = name.casefold()
        if key in seen:
            if seen[key] != name:
                raise ValueError('Case collision: ' + name)
            continue
        seen[key] = name
        source = next((child(root, name) for root in roots if child(root, name).is_file()), None)
        if source is None:
            raise FileNotFoundError('Resource missing in ordered roots: ' + name)
        row = dict(path=name, bytes=source.stat().st_size, sha256=digest(source), source=str(source),
                   udpFallback=udp_compatible(name))
        rows.append(row)
    result = dict(schema=SCHEMA, roots=[str(p) for p in roots], files=sorted(rows, key=lambda r: r['path']))
    manifest(result)
    return result


def commit_release(staging, output, cancel=None):
    # Windows scanners can briefly hold newly closed files without delete
    # sharing. Keep atomic publication and retry only those sharing/access
    # failures, for at most 1.55 seconds; never replace an existing release.
    for attempt in range(6):
        if cancel and cancel.is_set():
            raise InterruptedError('Publication cancelled')
        if output.exists():
            raise FileExistsError('Release appeared during publication')
        try:
            staging.rename(output)
            return
        except PermissionError as error:
            if os.name != 'nt' or error.winerror not in (5, 32, 33) or attempt == 5:
                raise
            delay = .05 * 2 ** attempt
            if cancel:
                cancel.wait(delay)
            else:
                time.sleep(delay)


def publish(value, output, url, cancel=None):
    manifest(value)
    url = base_url(url)
    output = plain(output)
    if output.exists():
        raise FileExistsError('Use a new release directory')
    roots = [plain(p) for p in value['roots']]
    if any(output.is_relative_to(p) or p.is_relative_to(output) for p in roots):
        raise ValueError('Release overlaps game sources')
    # Validate every input before any output mutation. The second hash while
    # copying detects a source replaced during publication.
    for row in value['files']:
        p = plain(row['source'])
        if not any(p == child(root, row['path']) for root in roots):
            raise ValueError('Plan source is outside declared roots')
        if p.stat().st_size != row['bytes'] or digest(p) != row['sha256']:
            raise ValueError('Source changed: ' + row['path'])
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=output.name + '.partial-', dir=output.parent))
    try:
        files = []
        blob_dir = staging / '_fastdl'
        blob_dir.mkdir()
        for row in value['files']:
            if cancel and cancel.is_set():
                raise InterruptedError('Publication cancelled')
            target = child(staging, row['path'])
            target.parent.mkdir(parents=True, exist_ok=True)
            compressed = blob_dir / 'current.tmp'
            check, written = hashlib.sha256(), 0
            with plain(row['source']).open('rb') as src, target.open('xb') as raw, compressed.open('xb') as encoded:
                with gzip.GzipFile(filename='', mode='wb', fileobj=encoded, compresslevel=6, mtime=0) as gz:
                    while data := src.read(CHUNK):
                        if cancel and cancel.is_set():
                            raise InterruptedError('Publication cancelled')
                        written += len(data)
                        if written > row['bytes']:
                            raise ValueError('Source grew during copy')
                        check.update(data)
                        raw.write(data)
                        gz.write(data)
            if written != row['bytes'] or check.hexdigest() != row['sha256']:
                raise ValueError('Source changed during copy')
            public = {k: row[k] for k in ('path', 'bytes', 'sha256', 'udpFallback')}
            if compressed.stat().st_size < written:
                key = digest(compressed)
                dest = blob_dir / (key + '.gz')
                if dest.exists():
                    compressed.unlink()
                else:
                    compressed.rename(dest)
                public['gzip'] = dict(path='_fastdl/' + key + '.gz', bytes=dest.stat().st_size, sha256=key)
            else:
                compressed.unlink()
            files.append(public)
        public = manifest(dict(schema=SCHEMA, files=files))
        data = canonical(public)
        (staging / 'manifest.json').write_bytes(data)
        # Config is a separately reviewed artifact; never load it automatically.
        # Keep dlfile enabled: HTTP failure must be able to use native fallback.
        (staging / 'server-fastdl.cfg').write_text(
            '// Review and exec on the intended server after publishing this release.\n'
            f'sv_downloadurl "{url}"\nsv_allowdownload 1\nsv_send_resources 1\nsv_allow_dlfile 1\n', encoding='ascii', newline='\n')
        if cancel and cancel.is_set():
            raise InterruptedError('Publication cancelled')
        commit_release(staging, output, cancel)
        return dict(directory=str(output), manifestSha256=hashlib.sha256(data).hexdigest(),
                    files=len(files), rawBytes=sum(r['bytes'] for r in files),
                    compressedBytes=sum(r.get('gzip', r)['bytes'] for r in files),
                    serverConfig=str(output / 'server-fastdl.cfg'))
    finally:
        if staging.exists():
            # Only our uniquely-created staging tree, verified under output.parent.
            checked = plain(staging)
            if checked.parent != plain(output.parent):
                raise ValueError('Unsafe staging cleanup')
            shutil.rmtree(checked)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    make = sub.add_parser('plan')
    make.add_argument('--root', action='append', required=True, type=Path)
    make.add_argument('--list', action='append', default=[], type=Path, help='Explicit .res-style file list')
    for kind in ('model', 'sound', 'generic', 'event'):
        make.add_argument('--reslist-' + kind, type=Path)
    make.add_argument('--output', required=True, type=Path)
    pub = sub.add_parser('publish')
    pub.add_argument('--plan', required=True, type=Path)
    pub.add_argument('--output', required=True, type=Path)
    pub.add_argument('--url', required=True)
    args = p.parse_args()
    if args.command == 'publish':
        print(json.dumps(publish(json.loads(plain(args.plan).read_text('utf-8')), args.output, args.url)))
        return
    names, captures = [], []
    for file in args.list:
        names.extend(read_res(file))
        captures.append(dict(path=str(plain(file)), sha256=digest(file), kind='explicit'))
    for kind in ('model', 'sound', 'generic', 'event'):
        file = getattr(args, 'reslist_' + kind)
        if file:
            values, skipped = read_reslist(file, kind)
            names.extend(values)
            captures.append(dict(path=str(plain(file)), sha256=digest(file), kind=kind, skipped=skipped))
    if not args.list and not all(getattr(args, 'reslist_' + k) for k in ('model', 'sound', 'generic', 'event')):
        p.error('Supply an explicit list or all four complete engine reslist captures')
    value = plan(args.root, names)
    value['captures'] = captures
    output = plain(args.output)
    if any(output.is_relative_to(plain(root)) for root in args.root):
        raise ValueError('Plan must be outside runtime roots')
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as f:
        f.write(canonical(value))
    print(json.dumps(dict(plan=str(output), files=len(value['files']), bytes=sum(r['bytes'] for r in value['files']))))


if __name__ == '__main__':
    main()
