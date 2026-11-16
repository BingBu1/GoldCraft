#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual resource coordinator and exercise its owner contracts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
CASES = ('not-ready', 'active', 'upload', 'no-init', 'callback-live', 'normal',
         'unchanged', 'cancel', 'stale', 'before-failure', 'after-failure',
         'native-failure', 'native-unwind', 'native-reset', 'base',
         'base-failure', 'disconnect-base', 'download-failure', 'failure-cancel', 'failure-reconnect',
         'progress-values', 'progress-start-cancel', 'progress-text-cancel', 'progress-advance-cancel',
         'native-progress-start-cancel', 'native-progress-text-cancel', 'native-progress-advance-cancel',
         'progress-stale', 'progress-reentrant')


def identity(path):
    path = Path(path).resolve(strict=True)
    return dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--includes', type=Path, required=True, help='JSON object with SDK arguments array')
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    out.mkdir(parents=True)
    env = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    include_args = json.loads(args.includes.read_text('utf-8-sig'))['arguments']
    assert all(arg.startswith(('-I', '/I')) for arg in include_args)
    sdk_source = ROOT / 'external/MetaHookSv-20261007/MetaHook/include/HLSDK/common/interface.cpp'
    fixture = ROOT / 'tests/native/resource_bridge_tests.cpp'
    sources = [fixture, sdk_source, ROOT / 'native/client/resource_bridge.cpp',
               ROOT / 'native/client/resource_bridge.hpp', Path(__file__)]
    inputs = [identity(p) for p in sources]
    result = dict(success=False, boundary='Production coordinator with controlled transport/owner interfaces; '
                  'not game execution or native HUD/WinHTTP implementation coverage.', inputs=inputs, cases=[])
    try:
        executable = out / 'resource_bridge_tests.exe'
        command = [str(args.compiler.resolve()), '--target=i686-pc-windows-msvc', '-fuse-ld=lld',
                   '/nologo', '/std:c++20', '/EHsc', '/MT', '/O2', '/clang:-O3', '/clang:-flto=thin',
                   '/arch:AVX2', '/fp:precise', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS',
                   '/DNO_MALLOC_OVERRIDE', '/I' + str(ROOT / 'native/include'), *include_args,
                   str(fixture), str(sdk_source), '/Fo' + str(out) + '\\', '/Fe' + str(executable),
                   '/link', '/OPT:REF', '/threads:1']
        build = subprocess.run(command, env=env, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (out / 'build.log').write_bytes(build.stdout)
        result['build'] = dict(exit=build.returncode, command=command)
        if build.returncode:
            raise RuntimeError(build.stdout.decode('utf-8', 'replace')[-6000:])
        for case in CASES:
            test = subprocess.run([str(executable), case], cwd=out, env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            (out / (case + '.log')).write_bytes(test.stdout)
            text = test.stdout.decode('utf-8', 'replace')
            print(text.strip(), flush=True)
            match = re.search(r'(\d+) checks, (\d+) failures', text)
            result['cases'].append(dict(name=case, exit=test.returncode,
                checks=int(match[1]) if match else None, failures=int(match[2]) if match else None))
        result['changed'] = [p['path'] for p in inputs if identity(p['path']) != p]
        result['executable'] = identity(executable)
        result['success'] = not result['changed'] and all(c['exit'] == 0 and c['failures'] == 0 for c in result['cases'])
    finally:
        (out / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
