#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test upgrade and both rollbacks over an issued patch in an owned stopped fixture."""
import argparse
import json
from pathlib import Path
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('package', 'prior-package', 'fixture', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    helpers = runpy.run_path(str(Path(__file__).with_name('run.py')))
    checked, sha = helpers['workspace'], helpers['sha']
    package, prior, target, out = map(checked, (args.package, args.prior_package, args.fixture, args.output))
    if (out.exists() or not out.is_relative_to(ROOT / 'build') or
            not target.is_relative_to(ROOT / 'build') or not target.is_dir()):
        raise ValueError('Use an existing owned build fixture and a fresh output')
    out.mkdir(parents=True)
    inputs = {str(p): sha(p) for root in (package, prior) for p in root.rglob('*') if p.is_file()}
    result = dict(success=False, gameLaunched=False, remoteModified=False, calls=[], checks=[])

    def snapshot():
        return {p.relative_to(target).as_posix(): sha(p) for p in target.rglob('*')
                if p.is_file() and '.bingsang-current-update' not in p.parts}

    def check(value, name):
        if not value:
            raise AssertionError(name)
        result['checks'].append(name)
        print('PASS ' + name, flush=True)

    def run(root, name, *flags):
        done = subprocess.run(['powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', str(root / 'Update-Server.ps1'), '-TargetDirectory', str(target), *flags],
            capture_output=True, timeout=600, creationflags=subprocess.CREATE_NO_WINDOW)
        (out / (name + '.log')).write_bytes(done.stdout + done.stderr)
        result['calls'].append(dict(name=name, exit=done.returncode))
        check(done.returncode == 0, name)

    try:
        original = snapshot()
        run(prior, 'install-issued-download-recovery')
        prior_state = snapshot()
        run(package, 'plan-over-issued-patch', '-Plan')
        check(snapshot() == prior_state, 'compatible-plan-read-only')
        run(package, 'install-over-issued-patch')
        manifest = json.loads((package / 'update-manifest.json').read_text('utf-8-sig'))
        check(all(sha(target / row['path']) == row['sha256'] for row in manifest['files']), 'compatible-payload-identities')
        run(package, 'restore-to-issued-patch', '-Restore')
        check(snapshot() == prior_state, 'exact-prior-patch-restored')
        run(prior, 'restore-issued-patch', '-Restore')
        check(snapshot() == original, 'exact-original-fixture-restored')
        check(all(sha(Path(name)) == digest for name, digest in inputs.items()), 'both-packages-unchanged')
        result['success'] = True
    finally:
        (out / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n', 'utf-8')


if __name__ == '__main__':
    main()
