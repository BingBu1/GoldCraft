#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run actual CoF action damage against a current sealed full server build."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
GROUPS = 'cof-damage,cof-death,cof-reentry,cof-cancel,cof-owner,kick-damage,kick-stagger,kick-death,kick-reentry,kick-cancel,kick-owner'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'environment', 'light-provider', 'sample', 'proof', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('sdk_runner', ROOT / 'tests/csol-rebuy/run_sdk.py')
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    out = sdk.checked(args.output, exists=False)
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Fresh workspace SDK output required')
    proof_path, sample = sdk.checked(args.proof), sdk.checked(args.sample)
    proof = sdk.read(proof_path)
    if not proof['beforeImplementation'] or sdk.digest(sample) != proof['mpSha256']:
        raise ValueError('Original CoF identity or pre-edit proof mismatch')
    raw = sample.read_bytes()
    nt = int.from_bytes(raw[60:64], 'little')
    count = int.from_bytes(raw[nt+6:nt+8], 'little')
    optional = nt + 24
    table = optional + int.from_bytes(raw[nt+20:nt+22], 'little')
    image = int.from_bytes(raw[optional+28:optional+32], 'little')
    for region in proof['regionsVerified']:
        rva, size = int(region['va'], 16) - image, region['size']
        data = None
        for index in range(count):
            section = table + index * 40
            va = int.from_bytes(raw[section+12:section+16], 'little')
            length = int.from_bytes(raw[section+16:section+20], 'little')
            if va <= rva and rva + size <= va + length:
                at = int.from_bytes(raw[section+20:section+24], 'little') + rva - va
                data = raw[at:at+size]
                break
        if data is None or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Original instruction region changed: ' + region['va'])
    provider_path = sdk.checked(args.light_provider)
    provider = sdk.read(provider_path)
    if not provider['success'] or not provider['inputsUnchanged']:
        raise ValueError('Actual engine lightstyle provider build did not pass')
    inputs = [sdk.identity(Path(__file__)), sdk.identity(proof_path), sdk.identity(sample),
              sdk.identity(provider_path), provider['artifact'], *provider['inputs']]
    for row in inputs:
        sdk.verify(row)
    env = dict(os.environ)
    env['GOLDCRAFT_TEST_LIGHT_PROVIDER'] = provider['artifact']['path']
    build = sdk.checked(args.build)
    command = [sys.executable, '-B', str(ROOT / 'tests/csol-rebuy/run_sdk.py'),
        '--receipt', str(build / 'receipt.json'), '--msbuild-log', str(build / 'msbuild.log'),
        '--source-inputs', str(build / 'sources/source-inputs.json'),
        '--environment', str(sdk.checked(args.environment)), '--link-inputs', str(build / 'link-inputs.json'),
        '--output', str(out), '--fixture', str(Path(__file__).with_name('sdk.cpp')),
        '--resources', str(provider_path.parent),
        '--adapter-dir', str(ROOT / 'build/scenario-sdk-current-20261108-05'), '--groups', GROUPS]
    result = subprocess.run(command, cwd=ROOT, env=env)
    unchanged = True
    try:
        for row in inputs:
            sdk.verify(row)
    except ValueError:
        unchanged = False
    record = dict(success=result.returncode == 0 and unchanged, inputsUnchanged=unchanged,
        inputs=inputs, command=command, exit=result.returncode, originalRegions=len(proof['regionsVerified']),
        boundary='Actual controlled-engine SDK damage; no deployment, gameplay or live performance')
    out.with_name(out.name + '-driver.json').write_text(json.dumps(record, indent=2) + '\n', 'utf8')
    return 0 if record['success'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
