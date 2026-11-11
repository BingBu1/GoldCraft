#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile one actual Teddy client TU test; record actual include identities."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    raw = path.read_bytes()
    return dict(path=str(path.resolve()), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--oracle', type=Path)
    args = ap.parse_args()
    out = args.output.resolve()
    assert not out.exists(), 'Use a fresh output directory'
    out.mkdir(parents=True)
    compile_db = ROOT / 'build/csol-client-crc/client/compile_commands.json'
    row = next(r for r in json.loads(compile_db.read_text('utf8')) if r['file'].endswith('/frozen_smoke.cpp'))
    command = shlex.split(row['command'], posix=False)
    compiler = command.pop(0).strip('"')
    flags = []
    for token in command:
        if token == '--':
            break
        if token in ('-c', '/c') or token.lower().startswith(('/fo', '/fd', '/yu', '/fp:')):
            continue
        flags.append(token)
    flags += ['/Y-', '/fp:precise', '/clang:-ffp-contract=off', '/showIncludes']
    environment_path = ROOT / 'build/resource-lifecycle-engine-20261108-inputs/environment.json'
    controls = [identity(p) for p in (Path(__file__), compile_db, environment_path)]
    if args.oracle:
        controls.append(identity(args.oracle))
    env = os.environ | json.loads(environment_path.read_text('utf8'))
    env.update(TEMP=str(out), TMP=str(out), VSLANG='1033')
    source = Path(__file__).with_name('client.cpp')
    roots = [ROOT / 'native/csol', ROOT / 'tests/csol-teddy-sparks']
    roots += [Path(flag[2:]) for flag in flags if flag.startswith(('-I', '/I'))]
    before = {}
    for root in roots:
        for path in root.rglob('*'):
            if path.is_file() and path.suffix.lower() in ('.h', '.hpp', '.hh', '.inl', '.inc', '.cpp', '.c'):
                key = str(path.resolve()).casefold()
                if key not in before:
                    before[key] = identity(path)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    jobs = []

    def run(name, argv):
        result = subprocess.run(argv, cwd=ROOT, env=env, text=True, encoding='utf8', errors='replace', capture_output=True)
        (out / (name + '.stdout.txt')).write_text(result.stdout, 'utf8')
        (out / (name + '.stderr.txt')).write_text(result.stderr, 'utf8')
        jobs.append(dict(name=name, command=argv, exit=result.returncode))
        if result.returncode:
            raise RuntimeError(name + ' failed: ' + result.stderr[-6000:] + result.stdout[-3000:])
        return result.stdout

    try:
        output = run('compile', [compiler, *flags, '/Fo' + str(out / 'client.obj'), '/Fd' + str(out / 'compile.pdb'), '-c', '--', str(source)])
        dependencies = {str(source.resolve()).casefold()}
        for line in output.splitlines():
            match = re.search(r'Note: including file:\s*(.*?)\s*$', line)
            if match:
                path = Path(match.group(1)).resolve()
                if path.is_relative_to(ROOT):
                    dependencies.add(str(path).casefold())
        actual = []
        for key in sorted(dependencies):
            assert key in before, 'Included dependency was not captured before compilation: ' + key
            old = before[key]
            assert identity(Path(old['path'])) == old, 'Source changed during compilation: ' + old['path']
            actual.append(old)
        exe = out / 'client.exe'
        run('link', [compiler, '/nologo', '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT',
                     str(out / 'client.obj'), '/Fe' + str(exe), '/link', '/machine:x86', '/OPT:REF', '/OPT:ICF', '/threads:1'])
        test = run('test', [str(exe)])
        assert json.loads(test)['success']
        comparison = None
        if args.oracle:
            original = json.loads(args.oracle.read_text('utf8'))
            assert original['success'] and original['sample']['sha256'] == 'f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd'
            rows = [case for case in original['cases'] if case['name'].startswith('packet-')]
            packets, expected = [], []
            for case in rows:
                for slot, (packet, emitter, particle) in enumerate(zip(case['source']['packets'], case['parsed'], case['frames'][0]['particles'])):
                    packets.append(packet['payload'])
                    expected.append([slot, emitter['rotation'], emitter['interval'], *emitter['position'],
                                     *emitter['color'], *particle['velocity'], 10.5, particle['die'], particle['rotation'][2]])
            process = subprocess.run([str(exe), '--emitter-oracle'], input='\n'.join(packets) + '\n',
                                     cwd=ROOT, env=env, text=True, capture_output=True, timeout=60, check=True)
            (out / 'emitter.stdout.txt').write_text(process.stdout, 'utf8')
            (out / 'emitter.stderr.txt').write_text(process.stderr, 'utf8')
            values = [json.loads(line) for line in process.stdout.splitlines()]
            assert len(values) == len(expected)
            failures = []
            for index, (actual_values, wanted) in enumerate(zip(values, expected)):
                if len(actual_values) != len(wanted) or any(struct.pack('<f', a) != struct.pack('<f', b) for a, b in zip(actual_values, wanted)):
                    failures.append(dict(index=index, actual=actual_values, expected=wanted))
            comparison = dict(success=not failures, oracle=identity(args.oracle), cases=len(values),
                              fields=sum(map(len, expected)), failures=failures,
                              boundary='Actual Receive parser and actual Spawn against original parser/Add/Frame initialization; actual Frame integrates after spawning and is tested separately')
            (out / 'emitter-comparison.json').write_text(json.dumps(comparison, indent=2), 'utf8')
            assert not failures, 'Original emitter initialization mismatch'
        assert all(identity(Path(item['path'])) == item for item in actual)
        assert all(identity(Path(item['path'])) == item for item in controls)
        result = dict(success=True, head=head, dependencies=actual, inputsUnchanged=True,
                      controls=controls, originalEmitterComparison=comparison,
                      compileDatabase=identity(compile_db), environment=identity(environment_path),
                      executable=identity(exe), jobs=jobs, test=json.loads(test), deployed=False,
                      boundaries=dict(session='explicit no-op SynchronizeSession fixture', runtime='actual runtime.hpp and SDK; actual production TU included directly'))
        (out / 'result.json').write_text(json.dumps(result, indent=2), 'utf8')
        print(json.dumps(dict(success=True, executable=str(exe), dependencies=len(actual))))
    except Exception:
        (out / 'failure.json').write_text(json.dumps(dict(success=False, head=head, jobs=jobs), indent=2), 'utf8')
        raise


if __name__ == '__main__':
    main()
