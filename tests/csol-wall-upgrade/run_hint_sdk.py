#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze the wall indicator consumer and run it with the real x86 client SDK."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    path = Path(path).resolve(strict=True)
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists()
    for p in (out, *out.parents):
        assert not p.is_junction() and not p.is_symlink(), p
    previous = ROOT/'build/csol-oberon-hint-20261031/client-validation-01'
    plan = json.loads((previous/'build-proposal.json').read_text('utf-8'))
    out.mkdir()
    inputs, sources = [], []
    for relative in ('tests/csol-wall-upgrade/hint.cpp', 'native/csol/scenario/core/wall_upgrade.hpp',
                     'native/csol/scenario/core/wall_upgrade_hint.hpp', 'native/csol/client/scenario/core/wall_upgrade_hint.hpp',
                     'native/csol/client/scenario/core/wall_upgrade_hint.cpp', 'native/csol/client/runtime.hpp'):
        source, target = ROOT/relative, out/'source'/relative
        before = identity(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())
        assert identity(source) == before and target.stat().st_nlink == 1
        inputs.append(identity(target))
        sources.append(dict(relative=relative, source=before, frozen=inputs[-1]))
    runner = out/'run.py'
    runner.write_bytes((previous/'run.py').read_bytes())
    inputs += [identity(runner), identity(__file__), identity(previous/'build-proposal.json')]
    job = plan['jobs'][0]
    argv = []
    for token in job['compile']:
        if token.endswith('tests\\csol-oberon-hint\\main.cpp'):
            token = str(out/'source/tests/csol-wall-upgrade/hint.cpp')
        elif token.endswith('native\\csol\\client\\oberon_hint.cpp'):
            token = str(out/'source/native/csol/client/scenario/core/wall_upgrade_hint.cpp')
        elif token.startswith('/Fo'):
            token = '/Fo'+str(out/'actual-client')+'/'
        elif token.startswith('/Fe'):
            token = '/Fe'+str(out/'actual-client/tests.exe')
        argv.append(token)
    argv.insert(argv.index('/link'), '/clang:-ffp-contract=off')
    inputs.append(identity(argv[0]))
    job.update(directory=str(out/'actual-client'), compile=argv,
               executable=str(out/'actual-client/tests.exe'), compileSeconds=180)
    plan.update(inputs=inputs, sources=sources, command=[sys.executable,str(runner),'--execute-authorized-plan'],
                boundary='Actual wall indicator client TU and fixed-width parser; real frozen x86 SDK. Engine lookup/allocation callbacks controlled; no complete plugin, deployment, real fade loop or visual gameplay.')
    (out/'build-proposal.json').write_text(json.dumps(plan, indent=2), 'utf-8')
    raise SystemExit(subprocess.call(plan['command'],cwd=ROOT))


if __name__ == '__main__':
    main()
