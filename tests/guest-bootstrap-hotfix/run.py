#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual guest-script installer, idempotence and rollback."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('package', 'baseline', 'fixture-baseline', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    package, baseline, previous, output = (p.resolve() for p in (
        args.package, args.baseline, args.fixture_baseline, args.output))
    if output.exists() or not output.is_relative_to(ROOT / 'build') or not previous.is_relative_to(ROOT / 'build'):
        raise ValueError('Use an existing isolated baseline and fresh workspace output')
    output.mkdir()
    target = output / 'server with spaces'
    target.mkdir()
    manifest = json.loads((package / 'update-manifest.json').read_text('utf8'))
    report = dict(success=False, checks=[], remoteModified=False, gameplay=False)
    def check(ok, message):
        if not ok:
            raise AssertionError(message)
        report['checks'].append(message)
        print(message, flush=True)
    def copy(source, name, digest):
        if not source.resolve().is_relative_to(ROOT) or source.is_symlink() or source.is_junction() or sha(source) != digest:
            raise ValueError('Fixture source differs: ' + name)
        dest = target / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        if source.suffix.lower() in ('.mdl', '.spr', '.seq', '.wav', '.mp3', '.tga', '.bmp', '.png', '.wad', '.bsp', '.nav', '.ttf', '.otf', '.cso'):
            os.link(source, dest)
            assert os.path.samefile(source, dest)
        else:
            shutil.copy2(source, dest)
    def snapshot():
        return {p.relative_to(target).as_posix(): sha(p) for p in target.rglob('*')
                if p.is_file() and '.bingsang-current-update' not in p.parts}
    def run(name, *flags, success=True):
        env = dict(os.environ)
        env.pop('PSModulePath', None)
        result = subprocess.run(['powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', str(package / 'Update-Server.ps1'), '-TargetDirectory', str(target), *flags],
            env=env, capture_output=True, timeout=240, creationflags=subprocess.CREATE_NO_WINDOW)
        (output / (name + '.log')).write_bytes(result.stdout + result.stderr)
        check((result.returncode == 0) == success, name)
    try:
        for row in manifest['requiredBaseline']:
            sources = [baseline / 'payload' / row['path'], previous / row['path']]
            source = next((p for p in sources if p.is_file() and sha(p) == row['sha256']), None)
            if not source:
                raise ValueError('Missing issued baseline: ' + row['path'])
            copy(source, row['path'], row['sha256'])
        for name in ('Start.cmd', 'Stop.cmd', 'server-settings.json', 'scripts/Run-Server.ps1',
                     'scripts/Start-Servers.ps1', 'cs/hlds.exe', 'cs/cstrike/game_init.cfg', 'cs/cstrike/server.cfg'):
            if not (target / name).exists():
                source = previous / name
                copy(source, name, sha(source))
        config = target / 'cs/cstrike/addons/goldcraft/config/gameplay.json'
        config.parent.mkdir(parents=True, exist_ok=True)
        config.write_text('{"operator":"keep exact bytes","stamina":15}\n', encoding='utf8')
        before = snapshot()
        run('plan', '-Plan')
        check(snapshot() == before, 'plan changes no bytes')
        row = manifest['files'][0]
        unknown = target / row['path']
        unknown.parent.mkdir(parents=True, exist_ok=True)
        unknown.write_text('unknown operator file', encoding='utf8')
        run('unknown-script-preserved', '-Plan', success=False)
        check(unknown.read_text('utf8') == 'unknown operator file', 'unknown existing script preserved')
        unknown.unlink()
        run('actual-install')
        check(all(sha(target / row['path']) == row['sha256'] for row in manifest['files']), 'all six scripts installed')
        check(sha(config) == before[config.relative_to(target).as_posix()], 'operator settings retained byte for byte')
        installed = snapshot()
        run('repeat-idempotent')
        check(snapshot() == installed, 'repeat leaves every byte intact')
        run('actual-rollback', '-Restore')
        check(snapshot() == before, 'rollback removes six scripts and preserves the full baseline')
        spec = importlib.util.spec_from_file_location('resources', ROOT / 'tools/client-resources/package.py')
        resources = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(resources)
        check(all(resources.selected(row['path'].removeprefix('cs/cstrike/')) for row in manifest['files']),
              'resource inventory accepts every actual required guest script')
        check(not any(resources.selected(p) for p in ('configs/secret.json', 'inventoryitems/private.txt',
                  'aurora/private.aur', 'aurora/guest/cof/tool.exe')), 'resource inventory keeps unrelated scripts and configs excluded')
        report['success'] = True
    finally:
        (output / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
