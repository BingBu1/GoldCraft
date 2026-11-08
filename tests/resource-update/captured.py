"""Replay a captured corruption manifest through the production SHA updater.

This is an isolated VFS/HTTP test, not proof that the game advertises v4.
The source client is strictly read-only; replacement bodies are not supplied.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--client-downloads', type=Path, required=True)
    p.add_argument('--url', required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    out, source = args.output.resolve(), args.client_downloads.resolve(strict=True)
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use a fresh output directory under build')
    entries = json.loads(args.manifest.read_text('utf-8-sig'))['files']
    original = []
    lines = []
    names = set()
    for row in entries:
        name = row['path']
        if '\\' in name or any(c in name for c in '\r\n"\t'):
            raise ValueError('Invalid fixture resource name')
        path = (source / name).resolve(strict=True)
        if not path.is_relative_to(source) or name.lower() in names:
            raise ValueError('Escaping or duplicate fixture name')
        names.add(name.lower())
        if sha(path) != row['observedSha256'] or row['observedSha256'] == row['sha256']:
            raise ValueError('Captured bad sample changed: ' + name)
        original.append((path, row['observedSha256']))
        lines.append(f'"{name}" {int(row["bytes"])} {row["sha256"]}\n')
    out.mkdir(parents=True)
    for row, (path, _) in zip(entries, original):
        target = out / 'game' / row['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    (out / 'manifest.txt').write_text(''.join(lines), encoding='ascii')
    filesystem = ROOT / 'sandbox/cs-client-b/Half-Life/FileSystem_Stdio.dll'
    if sha(filesystem) != '07a432b6214c6ea5ea4a988d5c8384d3ab35a531b49fe71c354f3fed5ce9f316':
        raise ValueError('Filesystem identity differs from reviewed module')
    command = [str(args.exe.resolve()), str(filesystem), str(out), args.url, 'captured-corruption']
    with (out / 'run.log').open('w', encoding='utf-8') as log:
        result = subprocess.run(command, cwd=out, stdout=log, stderr=subprocess.STDOUT, timeout=240)
    preserved = all(sha(path) == expected for path, expected in original)
    bad_copies_preserved = all(sha(out / 'game' / row['path']) == row['observedSha256'] for row in entries)
    cache_verified = all(sha(out / 'cache/objects' / row['sha256'] / row['path']) == row['sha256'] for row in entries)
    receipt = dict(returncode=result.returncode, files=len(entries), originalsPreserved=preserved,
                   corruptCopiesPreserved=bad_copies_preserved, cacheVerified=cache_verified,
                   executableSha256=sha(args.exe), manifestSha256=sha(args.manifest),
                   boundary='Production Session + WinHTTP + reviewed VFS; not a game signon/consumer refresh test.')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print(json.dumps(receipt))
    return 0 if result.returncode == 0 and preserved and bad_copies_preserved and cache_verified else 1


if __name__ == '__main__':
    raise SystemExit(main())
