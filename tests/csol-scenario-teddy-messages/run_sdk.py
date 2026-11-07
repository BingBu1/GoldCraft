# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the production message writer against a frozen, real ReGameDLL SDK.

Pass a completed server build containing build-commands.json, environment.json,
source-overlay.json and effective-source-manifest.json. No DLL is deployed.
Only the two selected message sources and this test replace baseline inputs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def identity(path):
    path = Path(path).resolve(strict=True)
    raw = path.read_bytes()
    return dict(path=str(path), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--oracle', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out, base, source = (p.resolve() for p in (args.output, args.baseline, args.source))
    if out.exists():
        raise ValueError('Output must be a new directory; preserve earlier results')
    out.mkdir(parents=True)

    def save(name, value):
        (out / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), 'utf8')

    env = os.environ | read(base / 'environment.json')
    inputs = [entry['frozen'] for entry in read(base / 'effective-source-manifest.json')['files']]
    inputs += read(base / 'inputs.json')['files']
    inputs.append(identity(Path(__file__)))
    # These test-only standard headers are not used by the server's translation
    # units. Pin them from the supplied compiler environment before compilation.
    include_roots = [Path(value) for value in env['INCLUDE'].split(';') if value]
    for name in ('iostream', 'source_location'):
        candidates = [folder / name for folder in include_roots if (folder / name).is_file()]
        assert candidates, name
        inputs.append(identity(candidates[0]))
    inputs = list({str(Path(row['path']).resolve()).lower(): row for row in inputs}.values())
    for entry in inputs:
        assert identity(entry['path']) == entry, entry['path']
    overlay = read(base / 'source-overlay.json')
    paths = ['native/csol/server/scenario/core/scenario_messages.cpp', 'native/csol/server/scenario/core/scenario_messages.h',
             'tests/csol-scenario-teddy-messages/main.cpp']
    selected = []
    for relative in paths:
        original = (root if relative.startswith('tests/') else source) / relative
        frozen = out / 'source' / relative
        frozen.parent.mkdir(parents=True, exist_ok=True)
        frozen.write_bytes(original.read_bytes())
        selected.append(identity(frozen))
        for row in overlay['roots']:
            if row['name'].replace('\\', '/').lower().endswith('/' + relative.lower()):
                row['external-contents'] = str(frozen)
        for candidate in (root / relative, original, frozen):
            overlay['roots'].append(dict(type='file', name=str(candidate), **{'external-contents': str(frozen)}))
    save('overlay.json', overlay)
    original = next(row['command'] for row in read(base / 'build-commands.json') if row['kind'] == 'compile')
    flags, skip = [], False
    for item in original:
        if skip:
            skip = False
            continue
        if item == '/clang:-ivfsoverlay':
            skip = True
            continue
        lower = item.lower()
        if lower.startswith(('/fo', '/fd', '/yu', '/fp', '/jmc', '/arch:', '/sourcedependencies')) or lower.endswith('.cpp'):
            # /fp:precise is re-applied below; no stale precompiled header.
            continue
        flags.append(item)
    flags += ['/arch:AVX2', '/fp:precise', '/I' + str(root), '/I' + str(root / 'native/csol/server'),
              '/clang:-ivfsoverlay', '/clang:' + str(out / 'overlay.json')]
    commands, objects = [], []
    for relative in (paths[0], paths[2]):
        frozen = out / 'source' / relative
        obj = out / (frozen.stem + '.obj')
        objects.append(str(obj))
        commands.append((frozen.stem, flags + [str(frozen), '/Fo' + str(obj), '/Fd' + str(out / 'compile.pdb')]))
    linker = Path(flags[0]).with_name('lld-link.exe')
    exe = out / 'messages.exe'
    commands.append(('link', [str(linker), '/NOLOGO', '/MACHINE:X86', '/SUBSYSTEM:CONSOLE',
                              '/OPT:REF', '/OPT:ICF', '/OUT:' + str(exe), *objects]))
    commands.append(('behavior', [str(exe)]))
    if args.oracle:
        proof = read(args.oracle.parent / 'result.json')
        assert proof['success'] and proof['cases']
        assert identity(args.oracle) == proof['fixture']
        assert int(args.oracle.read_text('ascii').splitlines()[0]) == len(proof['cases'])
        inputs.append(identity(args.oracle))
        inputs.append(identity(args.oracle.parent / 'result.json'))
        commands.append(('original-comparison', [str(exe), str(args.oracle.resolve())]))
        for name, content in [('empty', ''), ('truncated', args.oracle.read_text('ascii').rsplit('\n', 3)[0] + '\n'),
                              ('partial', '1\n2 96 0 0 0 1\n')]:
            fixture = out / (name + '.txt')
            fixture.write_text(content, 'ascii')
            commands.append(('reject-' + name, [str(exe), str(fixture)]))
    save('plan.json', dict(inputs=inputs + selected, commands=commands, deployment=False))
    steps = []
    for name, command in commands:
        start = time.monotonic()
        result = subprocess.run(command, cwd=out, env=env, capture_output=True, timeout=240)
        log = out / (name + '.log')
        log.write_bytes(result.stdout + result.stderr)
        expectedFailure = name.startswith('reject-')
        passed = result.returncode == 1 if expectedFailure else result.returncode == 0
        steps.append(dict(name=name, exit=result.returncode, expectedFailure=expectedFailure, passed=passed,
                          seconds=time.monotonic() - start, log=identity(log)))
        changed = [row['path'] for row in selected if identity(row['path']) != row]
        save('result.json', dict(steps=steps, changedInputs=changed, deployment=False))
        print(name, result.returncode, log.read_text('utf8', errors='replace')[-1200:] if result.returncode or name in
              ('behavior', 'original-comparison') else '', flush=True)
        assert not changed, changed
        if not passed:
            raise SystemExit(result.returncode or 1)
    known = {str(Path(row['path']).resolve()).lower(): row for row in inputs + selected}
    aliases = {str(Path(row['name']).resolve()).lower(): str(Path(row['external-contents']).resolve()).lower()
               for row in overlay['roots']}
    observed = set()
    for name in ('scenario_messages', 'main'):
        for line in (out / (name + '.log')).read_text('utf8', errors='strict').splitlines():
            if line.startswith('Note: including file:'):
                path = str(Path(line.split('file:', 1)[1].strip()).resolve()).lower()
                observed.add(aliases.get(path, path))
    unknown = sorted(observed - known.keys())
    changed = [row['path'] for row in inputs + selected if identity(row['path']) != row]
    save('dependency-audit.json', dict(success=not unknown and not changed, observed=len(observed),
                                     unknown=unknown, changed=changed))
    assert not unknown and not changed, (unknown, changed)


if __name__ == '__main__':
    main()
