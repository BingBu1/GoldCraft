#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual repair installer and rollback in a separate Windows root."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    with Path(path).open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def main(args):
    package, baseline, previous, out = (p.resolve(strict=True) if p.exists() else p.resolve()
        for p in (args.package, args.baseline, args.baseline_fixture, args.output))
    if out.exists() or not out.is_relative_to(ROOT/'build') or not previous.is_relative_to(ROOT/'build'):
        raise ValueError('Use an isolated fresh workspace fixture')
    out.mkdir(parents=True)
    target = args.fixture.resolve(strict=True) if args.fixture else out/'server with spaces'
    if not target.is_relative_to(ROOT/'build') or target.is_symlink() or target.is_junction():
        raise ValueError('Fixture must be an independent workspace path')
    target.mkdir(exist_ok=True)
    manifest = json.loads((package/'update-manifest.json').read_text())
    report = dict(success=False, checks=[], calls=[], gameplay=False, remoteModified=False)

    def check(value, text):
        if not value:
            raise AssertionError(text)
        report['checks'].append(text)
        print('PASS '+text, flush=True)

    def copy(source, name, expected):
        if source.is_symlink() or source.is_junction() or sha(source) != expected:
            raise ValueError('Fixture source identity mismatch: '+name)
        dest = target/name
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.is_file():
            if sha(dest) != expected:
                raise ValueError('Reused fixture differs: '+name)
            return
        if source.suffix.lower() in ('.mdl', '.spr', '.seq', '.wav', '.mp3', '.tga', '.bmp', '.png', '.wad', '.bsp', '.nav', '.ttf', '.otf', '.cso'):
            os.link(source, dest)
            if not os.path.samefile(source, dest):
                raise ValueError('Fixture file identity mismatch')
        else:
            shutil.copy2(source, dest)

    def snapshot():
        return {p.relative_to(target).as_posix(): sha(p) for p in target.rglob('*')
                if p.is_file() and '.bingsang-current-update' not in p.parts}

    def run(label, *flags, success=True):
        env = dict(os.environ)
        env.pop('PSModulePath', None)
        result = subprocess.run(['powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', str(package/'Update-Server.ps1'), '-TargetDirectory', str(target), *flags],
            capture_output=True, timeout=240, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
        log = out/('call-'+str(len(report['calls']))+'.log')
        log.write_bytes(result.stdout+result.stderr)
        report['calls'].append(dict(label=label, code=result.returncode))
        check((result.returncode == 0) == success, label+'; '+str(log))

    try:
        for row in manifest['requiredBaseline']:
            candidates = [previous/row['path'], baseline/'payload'/row['path']]
            source = next((p for p in candidates if p.is_file() and sha(p) == row['sha256']), None)
            if source is None:
                raise ValueError('Missing issued fixture baseline: '+row['path'])
            copy(source, row['path'], row['sha256'])
        for name in ('server-settings.json', 'Start.cmd', 'Stop.cmd', 'cs/hlds.exe', 'scripts/Run-Server.ps1',
                     'scripts/Start-Servers.ps1', 'cs/cstrike/game_init.cfg', 'cs/cstrike/server.cfg',
                     'cs/cstrike/addons/amxmodx/configs/plugins.ini',
                     'cs/cstrike/addons/amxmodx/configs/modules.ini'):
            source = previous/name
            if source.is_file() and not (target/name).exists():
                copy(source, name, sha(source))
        old_csx = ROOT/'analysis/server-crash-20261115-173410/csx_amxx.dll'
        csx_name = 'cs/cstrike/addons/amxmodx/modules/csx_amxx.dll'
        csx_row = next(row for row in manifest['files'] if row['path'] == csx_name)
        check(sha(old_csx) in csx_row['priorSha256'], 'dump module is accepted previous CSX')
        copy(old_csx, csx_name, sha(old_csx))
        old_manifest = baseline/'payload/cs/launcher-public/manifest.json'
        copy(old_manifest, 'cs/launcher-public/manifest.json', sha(old_manifest))
        config = target/'cs/cstrike/addons/goldcraft/config/gameplay.json'
        config.parent.mkdir(parents=True, exist_ok=True)
        config.write_text('{"operator":"preserve-exactly","time_cycle":false}\n')
        before = snapshot()
        run('plan validates complete baseline without writes', '-Plan')
        check(snapshot() == before, 'plan leaves every file unchanged')
        required = manifest['requiredBaseline'][0]
        path = target/required['path']
        parked = path.with_name(path.name+'.test-parked')
        path.rename(parked)
        run('missing baseline stops before changes', '-Plan', success=False)
        parked.rename(path)
        check(snapshot() == before, 'failed preflight leaves original root unchanged')
        run('actual repair installs')
        check(all(sha(target/r['path']) == r['sha256'] for r in manifest['files']), 'all four payload identities installed')
        check(sha(config) == before[config.relative_to(target).as_posix()], 'operator gameplay settings retained byte for byte')
        client = json.loads(base64.b64decode(json.loads((target/'cs/launcher-public/manifest.json').read_text())['payload']))
        check(client['sequence'] == 2026111502, 'new client release served by existing launcher route')
        installed = snapshot()
        run('repeat update is idempotent')
        check(snapshot() == installed, 'repeat preserves all installed bytes')
        run('rollback restores crashed-module baseline', '-Restore')
        check(snapshot() == before, 'rollback restores exact previous root, removes new guest table')
        report['success'] = True
    finally:
        (out/'receipt.json').write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('package', 'baseline', 'baseline-fixture', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--fixture', type=Path)
    main(parser.parse_args())
