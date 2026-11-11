#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the real Teddy model builder and compare original-instruction packets.

Windows Python, invoked directly from Git Bash. No game, SDK source copy, VFS
overlay, runtime deployment or complete server build is performed here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def identity(path):
    path = Path(path).resolve(strict=True)
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('compiler', 'environment', 'oracle', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use a fresh output directory under workspace build/')
    proof_path = args.oracle.parent / 'result.json'
    proof = json.loads(proof_path.read_text('utf8'))
    oracle = identity(args.oracle)
    records = proof.get('cases')
    if (not proof.get('success') or not proof.get('inputsUnchanged') or oracle != proof.get('fixture') or
            not isinstance(records, list) or not records):
        raise ValueError('Successful nonempty original-instruction receipt required')
    packet_count = sum(len(row['packets']) for row in records)
    lines = args.oracle.read_text('ascii').splitlines()
    if not packet_count or int(lines[0]) != packet_count or len(lines) != packet_count + 1:
        raise ValueError('Original packet count does not match the receipt')
    saved = json.loads(args.environment.read_text('utf-8-sig'))
    env = dict(os.environ)
    for key, value in saved.items():
        for existing in list(env):
            if existing.casefold() == key.casefold():
                del env[existing]
        env[key] = value
    env['VSLANG'] = '1033'
    compiler = args.compiler.resolve(strict=True)
    linker = compiler.with_name('lld-link.exe')
    source = HERE / 'attack_model.cpp'
    inputs = [source, Path(__file__), args.environment, args.oracle, proof_path, compiler, linker,
              ROOT / 'native/csol/scenario/bosses/teddy_model_effect.hpp',
              ROOT / 'native/csol/character_skill_protocol.hpp', ROOT / 'native/csol/cso_tent.hpp']
    # Freeze actual external header candidates before the compiler starts.
    resource = subprocess.check_output([str(compiler), '-print-resource-dir'], env=env, text=True).strip()
    roots = [Path(value) for value in saved['INCLUDE'].split(';') if value] + [Path(resource) / 'include']
    watch = {row['path']: row for row in map(identity, inputs)}
    for directory in roots:
        if not directory.is_dir():
            raise ValueError('Missing toolchain include directory: ' + str(directory))
        for path in directory.rglob('*'):
            if path.is_file():
                row = identity(path)
                watch.setdefault(row['path'], row)
    out.mkdir(parents=True)
    (out / 'temp').mkdir()
    env['TEMP'] = env['TMP'] = str(out / 'temp')
    (out / 'inputs-before.json').write_text(json.dumps(list(watch.values()), indent=2), 'utf8')
    exe = out / 'attack-model.exe'
    command = [str(compiler), '--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT',
               '/utf-8', '/O2', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise',
               '/Y-', '/showIncludes', '-fuse-ld=lld', str(source), '/Fo' + str(out / 'fixture.obj'),
               '/Fe' + str(exe), '/link', '/OPT:REF', '/OPT:ICF', '/threads:1', '/opt:lldltojobs=1']
    tests = [('original', args.oracle, 0)]
    for name, data in [('empty', ''), ('truncated', '\n'.join(lines[:-1]) + '\n'),
                       ('partial', '1\n0 1\n'), ('extra', '\n'.join(lines) + '\nextra\n'),
                       ('bad-hex', lines[0] + '\n' + lines[1][:-1] + 'z\n' + '\n'.join(lines[2:]) + '\n')]:
        fixture = out / (name + '.txt')
        fixture.write_text(data, 'ascii')
        tests.append((name, fixture, 1))
    result = dict(success=False, sourceCopies=0, fullDLL=False, deployed=False, liveGameplay=False,
                  command=command, originalEvents=len(records), originalPackets=packet_count, steps=[])
    actual = set(watch_row['path'] for watch_row in map(identity, inputs))
    try:
        build = subprocess.run(command, cwd=out, env=env, capture_output=True, timeout=180)
        log = build.stdout + build.stderr
        (out / 'compile.log').write_bytes(log)
        result['compileExit'] = build.returncode
        if build.returncode:
            raise ValueError('Compile/link failed; see compile.log')
        dependencies = re.findall(r'Note: including file:\s*([^\r\n]+)', log.decode('utf-8-sig', errors='replace'))
        if not dependencies:
            raise ValueError('Compiler did not report actual includes')
        for name in dependencies:
            path = str(Path(name.strip()).resolve(strict=True))
            if path not in watch:
                raise ValueError('Unrecorded include: ' + path)
            actual.add(path)
        for name, fixture, expected in tests:
            done = subprocess.run([str(exe), str(fixture.resolve())], cwd=out, env=env, capture_output=True, timeout=30)
            text = (done.stdout + done.stderr).decode('utf8', errors='replace')
            (out / (name + '.log')).write_text(text, 'utf8')
            passed = (done.returncode == 0 if expected == 0 else done.returncode == 1 and text.startswith('ERROR '))
            if expected == 0:
                passed = passed and bool(re.search(r'originalCases=' + str(packet_count) + r' checks=[1-9]\d* failures=0', text))
            result['steps'].append(dict(name=name, exit=done.returncode, expectedExit=expected, passed=passed, output=text))
            if not passed:
                raise ValueError('Test failed: ' + name)
        result['executable'] = identity(exe)
        result['success'] = True
    finally:
        result['changedInputs'] = [path for path in sorted(actual) if identity(path) != watch[path]]
        result['inputsUnchanged'] = not result['changedInputs']
        result['success'] = result['success'] and result['inputsUnchanged']
        result['actualInputs'] = [watch[path] for path in sorted(actual)]
        (out / 'result.json').write_text(json.dumps(result, indent=2), 'utf8')
    print(json.dumps(dict(success=result['success'], inputsUnchanged=result['inputsUnchanged'],
                          steps=result['steps'])))
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
