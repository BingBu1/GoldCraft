#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze and test exact production gate, real activation/effect and input code."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'native/csol/server/scenario_active_skill.cpp'
LINKED = ['native/csol/server/scenario_character_skills/control.cpp',
          'native/csol/server/scenario_character_skills/catalog.cpp',
          'native/csol/server/scenario_active_skill_effects.cpp',
          'native/csol/scenario_abilities.cpp']


def record(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--old-ref', required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / 'build') or output.exists():
        raise ValueError('Use a fresh workspace build directory')
    output.mkdir(parents=True)
    temporary = output / 'tmp'
    temporary.mkdir()
    env = os.environ | {'TEMP': str(temporary), 'TMP': str(temporary)}
    frozen = {}
    pending = [ROOT / name for name in [SOURCE, *LINKED, 'tests/csol-character-skill-gate/main.cpp']]
    while pending:
        path = pending.pop().resolve()
        if path in frozen:
            continue
        if not path.is_relative_to(ROOT):
            raise ValueError('Source outside workspace')
        target = output / 'source' / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
        assert target.stat().st_nlink == 1
        frozen[path] = dict(original=record(path), frozen=record(target))
        for name in re.findall(rb'^\s*#\s*include\s*"([^"\r\n]+)"', path.read_bytes(), re.M):
            name = name.decode()
            found = next((p for p in (path.parent/name, ROOT/name) if p.is_file()), None)
            if found:
                pending.append(found)
    spec = importlib.util.spec_from_file_location('cpp_function', ROOT/'tests/common/cpp_function.py')
    extract = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(extract)
    old_ref = subprocess.check_output(['git', 'rev-parse', args.old_ref], cwd=ROOT, text=True).strip()
    old = subprocess.check_output(['git', 'show', old_ref+':'+SOURCE], cwd=ROOT)
    current = Path(frozen[ROOT/SOURCE]['frozen']['path']).read_bytes()
    signature = b'    bool GateSnapshot('
    variants = {}
    for label, data, expected in [('current', current, 0), ('old', old, 1)]:
        directory = output/label
        directory.mkdir()
        path = directory/'gate.inc'
        path.write_bytes(extract.function(data, signature)[2])
        variants[label] = dict(sourceSha256=hashlib.sha256(data).hexdigest(), gate=record(path), expectedExit=expected)
    flags = ['--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT', '/utf-8',
             '/W3', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise', '/Zc:strictStrings-']
    jobs = []

    def execute(name, command, expected=0):
        started = time.monotonic()
        result = subprocess.run(command, cwd=output, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180)
        log = output/(name+'.log')
        log.write_bytes(result.stdout)
        jobs.append(dict(name=name, command=list(map(str, command)), exitCode=result.returncode,
                         expectedExit=expected, seconds=time.monotonic()-started, log=record(log)))
        (output/'jobs.json').write_text(json.dumps(jobs, indent=2), encoding='utf8')
        print(name, result.returncode, flush=True)
        if result.returncode != expected:
            print(result.stdout.decode('utf8', 'replace')[-6000:])
            raise RuntimeError(name+' failed')
        return result.stdout

    success = False
    try:
        objects = []
        for index, name in enumerate(LINKED):
            obj = output/('linked'+str(index)+'.obj')
            execute('compile-linked'+str(index), [args.compiler, *flags, '/c',
                    '/I'+str(output/'source'), output/'source'/name, '/Fo'+str(obj)])
            objects.append(obj)
        for label, info in variants.items():
            obj, exe = output/(label+'.obj'), output/(label+'.exe')
            execute('compile-'+label, [args.compiler, *flags, '/c', '/I'+str(output/'source'),
                    '/I'+str(output/label), output/'source/tests/csol-character-skill-gate/main.cpp', '/Fo'+str(obj)])
            execute('link-'+label, [args.compiler, *flags, '/clang:-fuse-ld=lld', obj, *objects, '/Fe'+str(exe)])
            log = execute('run-'+label, [exe], info['expectedExit'])
            match = re.search(rb'skill gate checks=(\d+) failures=(\d+)', log)
            if not match:
                raise RuntimeError('Missing completed test summary')
            info.update(checks=int(match[1]), failures=int(match[2]))
            if label == 'old' and b'captured ready character133 no longer stuck Preparing' not in log:
                raise RuntimeError('Old code did not reproduce the captured regression')
        sdk = ROOT/'external/ReGameDLL_CS/regamedll'
        includes = [sdk/x for x in ('', 'hookers', 'common', 'dlls', 'engine', 'public',
                    'public/regamedll', 'pm_shared', 'regamedll', 'testsuite')]
        includes += [ROOT/'native/include', ROOT/'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include']
        defines = ['REGAMEDLL_ADD', 'REGAMEDLL_API', 'REGAMEDLL_FIXES', 'REGAMEDLL_SSE', 'REGAMEDLL_SELF',
                   'REGAMEDLL_CHECKS', 'UNICODE_FIXES', 'BUILD_LATEST', 'BUILD_LATEST_FIXES', 'CLIENT_WEAPONS',
                   'USE_QSTRING', '_CRT_SECURE_NO_WARNINGS', 'NDEBUG', 'NOMINMAX']
        execute('compile-sdk', [args.compiler, *flags, '/c', *['/I'+str(x) for x in includes],
                *['/D'+x for x in defines], output/'source'/SOURCE, '/Fo'+str(output/'production.obj')])
        success = True
    finally:
        unchanged = all(record(Path(value['original']['path'])) == value['original'] for value in frozen.values())
        result = dict(success=success and unchanged, inputsUnchanged=unchanged, oldRef=old_ref,
                      compiler=record(args.compiler), source=list(frozen.values()), variants=variants, jobs=jobs,
                      boundary='Exact GateSnapshot extraction, real control/effects/catalog/client gate/input; controlled action/runtime edges. One actual production TU with matched x86 ReGameDLL SDK. No full DLL, deployment or gameplay.')
        (output/'result.json').write_text(json.dumps(result, indent=2), encoding='utf8')
    if not unchanged:
        raise RuntimeError('Source changed during validation')


if __name__ == '__main__':
    main()
