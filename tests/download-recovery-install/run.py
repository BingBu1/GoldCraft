#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise an actual repair installer against an independent issued baseline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
STATE = '.bingsang-current-update'


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def workspace(path):
    path = path.absolute()
    if not path.resolve().is_relative_to(ROOT) or any(p.is_symlink() or p.is_junction() for p in (path, *path.parents)):
        raise ValueError('Use plain workspace paths')
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('package', 'baseline', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    package, baseline, out = map(workspace, (args.package, args.baseline, args.output))
    if out.exists() or not baseline.is_relative_to(ROOT/'build') or not out.is_relative_to(ROOT/'build'):
        raise ValueError('Use a fresh receipt and an isolated build baseline')
    out.mkdir(parents=True)
    target = out/'server with spaces'
    target.mkdir()
    manifest = json.loads((package/'update-manifest.json').read_text('utf-8'))
    report = dict(success=False, checks=[], calls=[], remoteModified=False, gameLaunched=False,
                  immutableLinkedFiles=0, immutableLinkedLogicalBytes=0)

    def check(value, label):
        if not value:
            raise AssertionError(label)
        report['checks'].append(label)
        print('PASS ' + label, flush=True)

    def put(path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        temp = path.with_name(path.name+'.fixture.tmp')
        temp.write_bytes(data)
        os.replace(temp, path)

    def snapshot(root):
        return {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*')
                if p.is_file() and STATE not in p.parts}

    def run(label, *extra, ok=True):
        result = subprocess.run(['powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', str(package/'Update-Server.ps1'), '-TargetDirectory', str(target), *extra],
            capture_output=True, timeout=600, creationflags=subprocess.CREATE_NO_WINDOW)
        log = out/(label+'.log')
        log.write_bytes(result.stdout+result.stderr)
        report['calls'].append(dict(label=label, exit=result.returncode, log=log.name))
        if (result.returncode == 0) != ok:
            raise AssertionError(log.read_text(errors='replace')[-5000:])
        check(True, label)

    try:
        # Test corruption and the updater replace files atomically. Share only
        # verified immutable assets; configs, DLLs and logs stay independent.
        before = snapshot(baseline)
        for relative in before:
            source = workspace(baseline/relative)
            dest = target/relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            if sha(source) != before[relative]:
                raise ValueError('Baseline changed before fixture creation: ' + relative)
            if source.suffix.lower() in ('.mdl', '.spr', '.seq', '.wav', '.mp3', '.tga',
                                         '.bmp', '.png', '.wad', '.bsp', '.nav', '.ttf', '.otf', '.cso'):
                os.link(source, dest)
                if not os.path.samefile(source, dest) or sha(dest) != before[relative]:
                    raise ValueError('Immutable fixture link identity changed')
                report['immutableLinkedFiles'] += 1
                report['immutableLinkedLogicalBytes'] += source.stat().st_size
            else:
                shutil.copy2(source, dest)
        original = snapshot(target)
        check(original == before, 'independent-baseline')
        package_before = snapshot(package)
        run('plan', '-Plan')
        check(snapshot(target) == original, 'plan-read-only')
        row = min(manifest['requiredBaseline'], key=lambda r: r['bytes'])
        path = target/row['path']
        saved = path.read_bytes()
        put(path, b'invalid baseline')
        corrupt = snapshot(target)
        run('bad-baseline', '-Plan', ok=False)
        check(snapshot(target) == corrupt, 'bad-baseline-preserved')
        put(path, saved)
        guest = [r for r in manifest['files'] if r['path'].startswith(('cs/cstrike/aurora/', 'cs/cstrike/inventoryitems/'))]
        check(len(guest) == 6, 'six-guest-scripts')
        path = target/guest[0]['path']
        check(not path.exists(), 'omitted-guest-baseline')
        put(path, b'operator resource')
        conflict = snapshot(target)
        run('unknown-script', '-Plan', ok=False)
        check(snapshot(target) == conflict, 'unknown-script-preserved')
        path.unlink()
        run('install')
        check(all(sha(target/r['path']) == r['sha256'] for r in manifest['files']), 'all-payload-sha')
        changed = {r['path'] for r in manifest['files']}
        check(all(sha(target/n) == h for n, h in original.items() if n not in changed), 'operator-and-baseline-preserved')
        installed = snapshot(target)
        run('repeat')
        check(snapshot(target) == installed, 'idempotent')
        run('restore', '-Restore')
        check(snapshot(target) == original, 'complete-rollback')
        for row in guest:
            put(target/row['path'], (package/'payload'/row['path']).read_bytes())
        with_guest = snapshot(target)
        run('install-with-guest')
        run('restore-with-guest', '-Restore')
        check(snapshot(target) == with_guest, 'preexisting-guest-preserved')
        check(snapshot(package) == package_before, 'package-unchanged')
        check(snapshot(baseline) == before, 'baseline-source-unchanged')
        report['success'] = True
    finally:
        (out/'receipt.json').write_text(json.dumps(report, indent=2)+'\n', 'utf-8')
        if report['success']:
            # Only this freshly created, verified test root may be removed.
            workspace(target)
            if target.parent != out or not target.resolve().is_relative_to(ROOT/'build'):
                raise ValueError('Unexpected cleanup target')
            shutil.rmtree(target)
    return 0 if report['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
