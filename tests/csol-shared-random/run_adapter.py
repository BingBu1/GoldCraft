#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build one self-contained CRT SDK fixture; no original math oracle is run."""
import argparse
import hashlib
import json
import os
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2), 'utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--fixture', choices=('crt_adapter', 'crt_lifetime'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    out.mkdir()
    (out / 'temp').mkdir()
    baseline = json.loads(args.baseline.read_text('utf-8-sig'))
    template = next(row['argv'] for row in baseline['commands'] if row['name'] == 'fixture')
    env = os.environ | baseline['environment']
    env['TEMP'] = env['TMP'] = str(out / 'temp')
    fixture = 'tests/csol-shared-random/' + args.fixture + '.cpp'
    pending, sources = [fixture], {}
    while pending:
        relative = pending.pop()
        if relative in sources:
            continue
        source = ROOT / relative
        frozen = out / 'source' / relative
        frozen.parent.mkdir(parents=True, exist_ok=True)
        frozen.write_bytes(source.read_bytes())
        sources[relative] = dict(original=identity(source), frozen=identity(frozen))
        for token in re.findall(r'^\s*#include\s+"([^"]+)"', frozen.read_text('utf-8-sig'), re.M):
            child = (source.parent / token).resolve()
            if child.is_file() and child.is_relative_to(ROOT) and not child.is_relative_to(ROOT / 'external'):
                pending.append(child.relative_to(ROOT).as_posix())
    command = []
    for arg in template:
        if arg == '/DTEST_SKULL1' or arg.startswith('/IH:') and 'generated' in arg:
            continue
        if arg.startswith('/Fo'):
            arg = '/Fo' + str(out / 'fixture.obj')
        elif arg.startswith('/Fd'):
            arg = '/Fd' + str(out / 'fixture.pdb')
        elif arg == template[-1]:
            arg = str(out / 'source' / fixture)
        command.append(arg)
    exe = out / 'fixture.exe'
    commands = [('compile', command), ('link', [template[0], '--target=i686-pc-windows-msvc',
        '-fuse-ld=lld', '/MT', str(out / 'fixture.obj'), '/Fe'+str(exe), '/link',
        '/OPT:REF', '/OPT:LLDLTO=3', '/threads:1']), ('run', [str(exe)])]
    steps = []
    for name, argv in commands:
        start = time.monotonic()
        result = subprocess.run(argv, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180)
        log = out / (name + '.log')
        log.write_bytes(result.stdout)
        includes = sorted(set(re.findall(r'Note: including file:\s*(.+)', result.stdout.decode('utf-8', 'replace'))))
        steps.append(dict(name=name, argv=argv, exit=result.returncode,
                          seconds=time.monotonic()-start, log=identity(log),
                          includes=[identity(p.strip()) for p in includes]))
        save(out / 'steps.json', steps)
        print(name, result.returncode, flush=True)
        if result.returncode:
            print(result.stdout.decode('utf-8', 'replace')[-5000:])
            raise RuntimeError(name + ' failed')
    inputs = [row['frozen'] for row in sources.values()] + [identity(__file__), identity(args.baseline)]
    actual = {row['path']: row for step in steps for row in step['includes']}
    changed = [row['path'] for row in [*inputs, *actual.values()] if identity(row['path']) != row]
    save(out / 'result.json', dict(success=not changed, sources=sources, inputs=inputs,
        steps=steps, actualIncludes=list(actual.values()), changed=changed, deployed=False,
        liveGameplay=False, boundary='Actual client adapter and SDK types with controlled imports; no original arithmetic or gameplay claim.'))
    assert not changed


if __name__ == '__main__':
    main()
