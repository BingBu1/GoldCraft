#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build in-place x86 download sources with a supplied, pinned compiler environment."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]


def identity(path):
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=pathlib.Path, default=ROOT / 'build/parallel-download')
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--environment', type=pathlib.Path, required=True)
    args = parser.parse_args()
    out, build = args.output.resolve(), args.build.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build') or not build.is_relative_to(ROOT / 'build'):
        raise RuntimeError('Use a fresh receipt directory and a workspace build cache')
    out.mkdir(parents=True)
    env = dict(os.environ)
    env.update(json.loads(args.environment.read_text('utf-8-sig')))
    inputs = {ROOT / 'cmake/goldcraft-asio.cmake', ROOT / 'tools/Prepare-Asio.py', pathlib.Path(__file__).resolve()}
    for relative in ('native/parallel_download', 'native/include', 'tests/parallel-download',
                     'external/MetaHookSv-20261007/MetaHook/include',
                     'external/MetaHookSv-20261007/Plugins/VGUI2Extension/include/Interface',
                     'build/dependencies/asio-1-38-2/include'):
        inputs.update(path for path in (ROOT / relative).rglob('*') if path.is_file())
    before = [identity(path) for path in sorted(inputs)]
    (out / 'inputs.json').write_text(json.dumps(before, indent=2), encoding='utf-8')
    start = time.monotonic()
    with (out / 'build.log').open('wb') as log:
        result = subprocess.run(['cmake', '--build', str(build), '--parallel', '2'], cwd=ROOT,
                                env=env, stdout=log, stderr=subprocess.STDOUT)
    changed = [x['path'] for x in before if identity(pathlib.Path(x['path'])) != x]
    artifacts = [identity(build / name) for name in ('ParallelDownload.dll', 'ParallelDownload.pdb',
                 'parallel_download_tests.exe', 'parallel_download_bridge_tests.exe',
                 'parallel_download_coroutine_tests.exe')] if not result.returncode else []
    receipt = dict(success=result.returncode == 0 and not changed, exit=result.returncode, changed=changed,
                   seconds=time.monotonic() - start, inputs=before, artifacts=artifacts,
                   revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                   sourceCompiledInPlace=True, deployed=False)
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print(json.dumps({k: receipt[k] for k in ('success', 'exit', 'changed', 'seconds')}), flush=True)
    raise SystemExit(0 if receipt['success'] else 1)


if __name__ == '__main__':
    main()
