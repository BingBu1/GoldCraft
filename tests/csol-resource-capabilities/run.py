#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay the actual missing-provider deployment regression with real DLLs."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('consumer', 'without-refresh', 'with-refresh', 'build', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build receipt directory')
    spec = importlib.util.spec_from_file_location('capabilities', ROOT / 'tools/csol_resource_capabilities.py')
    capabilities = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(capabilities)
    paths = [args.consumer, args.without_refresh, args.with_refresh,
             args.build / 'CMakeCache.txt', Path(__file__), Path(spec.origin)]
    before = {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    out.mkdir(parents=True)
    off = out / 'off'
    off.mkdir()
    (off / 'CMakeCache.txt').write_text('CSOL_RESOURCE_REFRESH:BOOL=OFF\n', 'utf8')
    cases = []
    for name, candidate, build, rejected in (
            ('old-provider-missing', args.without_refresh, None, True),
            ('old-cache-disabled', None, off, True),
            ('current-cache-enabled', None, args.build, False),
            ('current-provider-present', args.with_refresh, args.build, False)):
        try:
            result = capabilities.verify(args.consumer, candidate, build)
            assert not rejected, 'Expected the deployment to be rejected'
            assert result['refreshRequired']
            cases.append(dict(name=name, passed=True, result=result))
        except ValueError as failure:
            if not rejected:
                raise
            cases.append(dict(name=name, passed=True, expectedRejection=str(failure)))
    unchanged = all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == sha for path, sha in before.items())
    assert unchanged
    receipt = dict(success=True, inputsUnchanged=unchanged, cases=cases, inputs=before,
                   boundary='Actual failed and corrected DLL registration markers plus build configuration; no live provider or HUD rendering')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf8')
    print(json.dumps(dict(success=True, cases=len(cases), inputsUnchanged=unchanged)))


if __name__ == '__main__':
    main()
