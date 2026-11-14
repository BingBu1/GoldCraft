#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise CMake's actual x86 clang-cl command, including the old -- failure."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('database', 'environment', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace receipt directory')
    modules = []
    for name, path in (('scope', ROOT / 'tools/Verify-CSOLServerBuildScope.py'),
                       ('sdk', ROOT / 'tests/csol-rebuy/run_sdk.py')):
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        modules.append(module)
    scope, sdk = modules
    row = next(row for row in sdk.read(args.database)
               if Path(row['file']) == ROOT / 'native/common/wire.cpp')
    original = sdk.windows_arguments(row['command'])
    assert '--' in original
    env = dict(os.environ)
    for key, value in sdk.read(args.environment).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    paths = [Path(__file__), scope.__file__, sdk.__file__, args.database,
             args.environment, original[0], row['file'],
             *sorted((ROOT / 'native/include').rglob('*.hpp')),
             *sorted((ROOT / 'native/include').rglob('*.h'))]
    before = [sdk.identity(path) for path in paths]
    out.mkdir(parents=True)
    steps = []
    for legacy in (True, False):
        label = 'legacy' if legacy else 'fixed'
        dep, obj = out / (label + '.d'), out / (label + '.obj')
        command = scope.dependency_command(original, row['file'], dep, obj)
        if legacy:
            command.insert(command.index('/Y-'), '--')
        rsp = out / (label + '.rsp')
        rsp.write_text(subprocess.list2cmdline(command[1:]), 'utf8')
        done = subprocess.run([command[0], '@' + str(rsp)], cwd=row['directory'],
                              env=env, capture_output=True, timeout=60)
        log = done.stdout + done.stderr
        (out / (label + '.log')).write_bytes(log)
        if legacy:
            assert done.returncode and b'/Y-' in log and not dep.exists()
        else:
            assert not done.returncode and dep.is_file(), log.decode('utf8', 'replace')
            assert 'server-scope:' in dep.read_text('utf8')
            assert 'wire.cpp' in dep.read_text('utf8')
        assert not obj.exists(), 'Dependency replay must never compile an object'
        steps.append(dict(name=label, exit=done.returncode,
                          expectedFailure=legacy, objectWritten=False))
    stable = before == [sdk.identity(path) for path in paths]
    assert stable
    result = dict(success=True, checks=7, inputsUnchanged=stable, steps=steps,
                  inputs=before, deployed=False, gameplay=False)
    (out / 'receipt.json').write_text(json.dumps(result, indent=2), 'utf8')
    print(json.dumps(dict(success=True, checks=7, inputsUnchanged=stable)))


if __name__ == '__main__':
    main()
