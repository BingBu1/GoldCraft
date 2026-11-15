#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the verified-resource client and its consumers without source snapshots."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def identity(path):
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    args = parser.parse_args()
    out, build = args.output.resolve(), args.build.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build') or not build.is_relative_to(ROOT / 'build'):
        raise RuntimeError('Use a fresh workspace receipt directory and a workspace build cache')
    cache = (build / 'CMakeCache.txt').read_text('utf-8-sig')
    if 'GOLDCRAFT_RESOURCE_SYNC:BOOL=ON' not in cache:
        raise RuntimeError('The complete verified-resource consumer must remain enabled')
    out.mkdir(parents=True)
    env = dict(os.environ); env.update(json.loads(args.environment.read_text('utf-8-sig')))
    inputs = {ROOT / 'CMakeLists.txt', Path(__file__).resolve(), ROOT / 'tools/Prepare-Asio.py'}
    for relative in ('native/client', 'native/common', 'native/include', 'cmake', 'tests/native',
                     'external/MetaHookSv-20261007/MetaHook/include',
                     'external/MetaHookSv-20261007/MetaHook/thirdparty/capstone_fork/include',
                     'external/MetaHookSv-20261007/Plugins/Renderer/include',
                     'external/MetaHookSv-20261007/Plugins/Renderer/src',
                     'external/MetaHookSv-20261007/Plugins/VGUI2Extension/include',
                     'external/MetaHookSv-20261007/thirdparty/glew_fork',
                     'build/dependencies/asio-1-38-2/include'):
        inputs.update(path for path in (ROOT / relative).rglob('*') if path.is_file() and
                      path.suffix in ('.cpp', '.c', '.h', '.hpp', '.inc', '.json', '.def', '.cmake', '.txt'))
    before = [identity(path) for path in sorted(inputs)]
    (out / 'inputs.json').write_text(json.dumps(before, indent=2), encoding='utf-8')
    start = time.monotonic()
    command = ['cmake', '--build', str(build), '--parallel', '2', '--target', 'GoldCraft',
               'goldcraft_precache_client_tests', 'goldcraft_resource_bridge_tests']
    with (out / 'build.log').open('wb') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    changed = [x['path'] for x in before if identity(Path(x['path'])) != x]
    artifacts = [identity(build / name) for name in ('GoldCraft.dll', 'GoldCraft.pdb',
                 'goldcraft_precache_client_tests.exe', 'goldcraft_resource_bridge_tests.exe')] if not result.returncode else []
    receipt = dict(success=result.returncode == 0 and not changed, exit=result.returncode, changed=changed,
        seconds=time.monotonic()-start, inputs=before, artifacts=artifacts, sourceCompiledInPlace=True,
        revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(), deployed=False)
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print(json.dumps({key: receipt[key] for key in ('success', 'exit', 'changed', 'seconds')}), flush=True)
    raise SystemExit(0 if receipt['success'] else 1)


if __name__ == '__main__':
    main()
