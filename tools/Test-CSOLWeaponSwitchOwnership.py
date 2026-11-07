#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze/run real weapon dispatcher ownership checks and two old-TU controls.

Preparation does not compile or execute native code. Run requires a separately
authorized compiler window and processes each compilation/link sequentially.
"""
from pathlib import Path
import argparse
import datetime
import difflib
import hashlib
import json
import os
import re
import subprocess

ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / 'native/csol').is_dir() and (parent / 'external/MetaHookSv-20261007').is_dir())
SDK = Path('external/MetaHookSv-20261007/MetaHook/include')
INCLUDES = [Path('native/csol'), Path('native/csol/client'), SDK, SDK / 'Interface',
            *(SDK / 'HLSDK' / name for name in ('cl_dll', 'common', 'engine', 'pm_shared', 'public')),
            SDK / 'SourceSDK', SDK / 'SourceSDK/tier0', SDK / 'SourceSDK/tier1',
            Path('external/MetaHookSv-20261007/thirdparty/glew_fork/include')]
SOURCES = ['tests/csol-weapon-prediction/switch_ownership.cpp',
           *('native/csol/' + name for name in (
               'client/session.cpp', 'client/weapons/weapon_inventory.cpp', 'client/weapons/weapons.cpp',
               'client/crosshair.cpp', 'client/pickup_history.cpp', 'client/weapons/beamgun.cpp',
               'client/weapons/cannon.cpp', 'client/weapons/k3.cpp', 'beamgun_state.cpp', 'weapon_slots.cpp')),
           'tests/csol-weapon-slots/session_edges.cpp']
BASELINES = ('weapons', 'k3')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def digest(path):
    return sha(path.read_bytes())


def safe_output(path):
    path = path.absolute()
    if not path.resolve().is_relative_to(ROOT / 'build'):
        raise ValueError('Output must remain under workspace build')
    for node in (path, *path.parents):
        if node.is_symlink() or (hasattr(node, 'is_junction') and node.is_junction()):
            raise ValueError('Linked output refused: ' + str(node))
    return path


def prepare(output, source_root, baseline_root):
    output.mkdir(parents=True, exist_ok=False)
    frozen = output / 'source'
    records = {}

    def origin(relative):
        if relative.parts[0] == 'native':
            return source_root / relative
        if relative.parts[0] == 'external' and (source_root / relative).is_file():
            return source_root / relative
        return ROOT / relative

    pending = [Path(name) for name in SOURCES]
    while pending:
        relative = pending.pop()
        if relative in records:
            continue
        source = origin(relative)
        content = source.read_bytes()
        destination = frozen / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(content)
        if destination.stat().st_nlink != 1 or digest(destination) != sha(content):
            raise RuntimeError('Snapshot must be independent and byte-identical: ' + str(relative))
        records[relative] = dict(path=relative.as_posix(), origin=str(source), sha256=sha(content), size=len(content))
        for delimiter, name in re.findall(r'^\s*#\s*include\s*(["<])([^">]+)[">]',
                                         content.decode('utf-8', errors='replace'), re.M):
            candidates = ([relative.parent / name] if delimiter == '"' else [])
            candidates += [directory / name for directory in INCLUDES]
            for candidate in candidates:
                candidate = Path(os.path.normpath(str(candidate)))
                if origin(candidate).is_file():
                    pending.append(candidate)
                    break
    baselines = []
    for name in BASELINES:
        relative = Path('native/csol/client') / (name + '.cpp')
        source = baseline_root / relative
        content = source.read_bytes()
        target = frozen / relative.with_name(name + '.before.cpp')
        target.write_bytes(content)
        if target.stat().st_nlink != 1:
            raise RuntimeError('Baseline snapshot unexpectedly shared: ' + str(target))
        baselines.append(dict(path=target.relative_to(frozen).as_posix(), origin=str(source), sha256=sha(content)))
        difference = ''.join(difflib.unified_diff(content.decode('utf-8').splitlines(True),
                            (frozen / relative).read_text(encoding='utf-8').splitlines(True),
                            fromfile='before/' + relative.as_posix(), tofile='after/' + relative.as_posix()))
        (output / (name + '.diff')).write_text(difference, encoding='utf-8')
    runner = output / Path(__file__).name
    runner.write_bytes(Path(__file__).read_bytes())
    manifest = dict(at=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    inputs=[records[key] for key in sorted(records)], baselines=baselines,
                    sourceRoot=str(source_root), baselineRoot=str(baseline_root), sources=SOURCES,
                    runnerSha256=digest(runner),
                    boundary='Actual WeaponPostRunCmd, Current message decoder, production predictors and K3 HookEvent; '
                             'controlled stock/engine callbacks. Current weapon comes only through real messages. '
                             'No gameplay, GL rendering, deployed DLL or visual-fidelity claim.')
    (output / 'prepared.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f'Prepared {len(records)} independent inputs and two production baselines; no compiler invoked: {output}')


def run(output, clang):
    manifest = json.loads((output / 'prepared.json').read_text(encoding='utf-8'))
    frozen = output / 'source'
    for item in [*manifest['inputs'], *manifest['baselines']]:
        if digest(frozen / item['path']) != item['sha256'] or (frozen / item['path']).stat().st_nlink != 1:
            raise RuntimeError('Frozen source changed or shared: ' + item['path'])
    if digest(output / Path(__file__).name) != manifest['runnerSha256']:
        raise RuntimeError('Frozen runner changed')
    artifact = output / 'run'
    artifact.mkdir(exist_ok=False)
    flags = ['--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT', '/W3', '/utf-8',
             '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/arch:AVX2', '/fp:precise',
             '/DNDEBUG', '/DNOMINMAX', '/DNO_MALLOC_OVERRIDE', '/DGLEW_STATIC', '/DGLEW_NO_GLU',
             '/D_CRT_SECURE_NO_WARNINGS', *('/I' + str(frozen / name) for name in INCLUDES)]
    units = [(Path(source).stem, source) for source in manifest['sources']]
    units += [(name + '-before', 'native/csol/client/' + name + '.before.cpp') for name in BASELINES]
    compiles, runs = [], []
    receipt = dict(compiles=compiles, runs=runs, boundary=manifest['boundary'], complete=False)
    try:
        for label, source in units:
            command = [str(clang), *flags, '/showIncludes', '/c', str(frozen / source), '/Fo' + str(artifact / (label + '.obj'))]
            result = subprocess.run(command, cwd=artifact, capture_output=True, text=True, encoding='utf-8', errors='replace')
            (artifact / (label + '.compile.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
            compiles.append(dict(label=label, command=command, exit=result.returncode))
            if result.returncode:
                raise RuntimeError('Compile failed: ' + label)
        for label, before in (('current', None), ('old-weapons', 'weapons'), ('old-k3', 'k3')):
            names = [Path(source).stem for source in manifest['sources']]
            if before:
                names[names.index(before)] += '-before'
            exe = artifact / (label + '.exe')
            command = [str(clang), *flags, *(str(artifact / (name + '.obj')) for name in names),
                       '/Fe' + str(exe), '/link', '/MACHINE:X86', '/threads:1', '/opt:lldltojobs=1']
            link = subprocess.run(command, cwd=artifact, capture_output=True, text=True, encoding='utf-8', errors='replace')
            (artifact / (label + '.link.log')).write_text(link.stdout + link.stderr, encoding='utf-8')
            record = dict(label=label, command=command, linkExit=link.returncode, expectedTestExit=int(before is not None))
            runs.append(record)
            if link.returncode:
                raise RuntimeError('Link failed: ' + label)
            tested = subprocess.run([str(exe)], cwd=artifact, capture_output=True, text=True, encoding='utf-8', errors='replace')
            (artifact / (label + '.test.log')).write_text(tested.stdout + tested.stderr, encoding='utf-8')
            record.update(testExit=tested.returncode, output=tested.stdout + tested.stderr, exeSha256=digest(exe))
            print(label + ': ' + (tested.stdout.splitlines()[-1] if tested.stdout else str(tested.returncode)), flush=True)
            if tested.returncode != record['expectedTestExit']:
                raise RuntimeError('Unexpected test exit: ' + label)
        receipt['complete'] = True
    finally:
        receipt['frozenUnchanged'] = all(digest(frozen / item['path']) == item['sha256']
                                         for item in [*manifest['inputs'], *manifest['baselines']])
        receipt['originInputsUnchanged'] = all(digest(Path(item['origin'])) == item['sha256'] for item in manifest['inputs'])
        (artifact / 'receipt.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    if not receipt['frozenUnchanged']:
        raise RuntimeError('Frozen source changed during verification')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--prepare-only', action='store_true')
    mode.add_argument('--run-prepared', action='store_true')
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--baseline-root', type=Path)
    parser.add_argument('--clang', type=Path)
    args = parser.parse_args()
    if args.prepare_only and not all((args.source_root, args.baseline_root)):
        parser.error('--prepare-only requires --source-root and --baseline-root')
    if args.run_prepared and not args.clang:
        parser.error('--run-prepared requires --clang')
    output = safe_output(args.output)
    if args.prepare_only:
        prepare(output, args.source_root.resolve(), args.baseline_root.resolve())
    else:
        run(output, args.clang.resolve())


if __name__ == '__main__':
    main()
