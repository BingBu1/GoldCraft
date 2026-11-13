#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the in-place CSOL client in an existing CMake cache and seal inputs."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time
import re

ROOT = Path(__file__).resolve().parent.parent


def compiler_inputs(build, env):
    cache = (build / 'CMakeCache.txt').read_text('utf-8-sig')
    ninja = re.search(r'^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$', cache, re.M)[1].strip()
    dependencies = subprocess.check_output([ninja, '-C', str(build), '-t', 'deps'], env=env).decode('utf8')
    # Ninja repeats each shared header for every object. Deduplicate names
    # before Windows path resolution/stat calls instead of visiting them O(TU).
    names, current_object = set(), False
    for line in dependencies.splitlines():
        if line and not line.startswith(' '):
            current_object = line.replace('\\', '/').startswith('CMakeFiles/CSOLSupport.dir/')
        elif current_object and line.startswith('    '):
            names.add(line.strip())
    actual = set()
    for name in names:
        path = Path(name)
        if not path.is_absolute():
            path = build / path
        path = path.resolve(strict=True)
        if path.is_relative_to(ROOT) and not path.is_relative_to(ROOT / 'build'):
            actual.add(path)
    return actual, dependencies


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, choices=range(1, 9), default=2)
    parser.add_argument('--compiler-dependencies', action='store_true',
                        help='Seal the actual Ninja client dependency closure; report unrelated broad drift separately')
    args = parser.parse_args()
    build, out = args.build.resolve(strict=True), args.output.resolve()
    if not build.is_relative_to(ROOT / 'build') or not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use an existing workspace compiler cache and fresh receipt directory')
    spec = importlib.util.spec_from_file_location('guard', ROOT / 'tools/Build-CSOLCandidate.py')
    guard = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(guard)
    out.mkdir(parents=True)
    guard.begin(out / 'sources')
    baseline = {row['path'].casefold(): row for row in json.loads((out / 'sources/source-inputs.json').read_text('utf8'))}
    controls = [guard.identity(path) for path in (Path(__file__), build / 'CMakeCache.txt', build / 'build.ninja',
                                                build / 'CMakeFiles/rules.ninja', args.environment.resolve(strict=True))]
    controls.extend(row for row in baseline.values() if row['path'].endswith('CMakeLists.txt') or row['path'].endswith('.cmake'))
    environment = args.environment.resolve(strict=True)
    before = guard.identity(environment)
    env = dict(os.environ)
    for key, value in json.loads(environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    cold_dependencies = []
    if args.compiler_dependencies:
        cold, _ = compiler_inputs(build, env)
        cold_dependencies = [guard.identity(path) for path in sorted(cold)]
        baseline.update((row['path'].casefold(), row) for row in cold_dependencies)
    command = ['cmake', '--build', str(build), '--target', 'CSOLSupport', '--parallel', str(args.jobs)]
    start = time.monotonic()
    with (out / 'build.log').open('w', encoding='utf8') as log:
        done = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    unchanged, error = True, ''
    try:
        guard.check_sources(out / 'sources')
        if guard.identity(environment) != before:
            raise ValueError('Compiler environment changed')
    except Exception as failure:
        unchanged, error = False, str(failure)
    broad_unchanged, consumed = unchanged, []
    if args.compiler_dependencies and done.returncode == 0:
        try:
            actual, dependencies = compiler_inputs(build, env)
            (out / 'ninja-dependencies.txt').write_text(dependencies, 'utf8')
            if not actual or not any(path.name == 'human_kick_animation.cpp' for path in actual):
                raise ValueError('Ninja did not report the actual client source dependencies')
            for path in sorted(actual):
                row = guard.identity(path)
                if baseline.get(str(path).casefold()) != row:
                    raise ValueError('Actual compiler dependency changed or was unrecorded: ' + str(path))
                consumed.append(row)
            for row in controls:
                if guard.identity(Path(row['path'])) != row:
                    raise ValueError('Client build control changed: ' + row['path'])
            unchanged = True
        except Exception as failure:
            unchanged = False
            error += '; compiler dependency audit: ' + str(failure)
    artifact = build / 'CSOLSupport.dll'
    receipt = dict(success=done.returncode == 0 and unchanged, command=command, exit=done.returncode,
                   inputsUnchanged=unchanged, error=error, environment=before,
                   broadInputsUnchanged=broad_unchanged, actualCompilerInputs=consumed,
                   coldCompilerInputs=cold_dependencies,
                   guardScope='actual Ninja compiler dependency closure and controls' if args.compiler_dependencies else 'full workspace source inventory',
                   elapsedSeconds=time.monotonic() - start, sourceCopies=0,
                   artifact=guard.identity(artifact) if artifact.is_file() and not done.returncode else None,
                   revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                   deployed=False, gameplay=False)
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
    print(json.dumps({key: receipt[key] for key in ('success', 'exit', 'inputsUnchanged', 'artifact')}))
    raise SystemExit(0 if receipt['success'] else 1)


if __name__ == '__main__':
    main()
