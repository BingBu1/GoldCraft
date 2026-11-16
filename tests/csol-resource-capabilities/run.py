#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay the actual missing-provider deployment regression with real DLLs."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('consumer', 'without-refresh', 'with-refresh', 'build', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--without-moving-lights', type=Path)
    parser.add_argument('--with-moving-lights', type=Path)
    args = parser.parse_args()
    if bool(args.without_moving_lights) != bool(args.with_moving_lights):
        parser.error('Provide both actual Renderer versions')
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build receipt directory')
    spec = importlib.util.spec_from_file_location('capabilities', ROOT / 'tools/csol_resource_capabilities.py')
    capabilities = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(capabilities)
    paths = [args.consumer, args.without_refresh, args.with_refresh,
             args.build / 'CMakeCache.txt', Path(__file__), Path(spec.origin)]
    if args.with_moving_lights:
        paths.extend((args.without_moving_lights, args.with_moving_lights))
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
    if args.with_moving_lights:
        for name, candidate, renderer, rejected in (
                ('player-light-renderer-missing', args.with_refresh, None, True),
                ('player-light-renderer-old', args.with_refresh, args.without_moving_lights, True),
                ('player-light-renderer-paired', args.with_refresh, args.with_moving_lights, False),
                ('legacy-client-renderer', args.without_refresh, args.without_moving_lights, False)):
            try:
                result = capabilities.verify_renderer(candidate, renderer)
                assert not rejected, 'Expected the Renderer pair to be rejected'
                cases.append(dict(name=name, passed=True, result=result))
            except ValueError as failure:
                if not rejected:
                    raise
                cases.append(dict(name=name, passed=True, expectedRejection=str(failure)))
        prefix = 'install/output/svencoop/metahook/plugins/'
        rows = [dict(path=prefix + 'CSOLSupport.dll', source=str(args.with_refresh)),
                dict(path=prefix + 'Renderer_AVX2.dll', source=str(args.with_moving_lights))]
        assert capabilities.verify_renderer_rows(rows, 'install/output/svencoop/')['movingLightsRequired']
        cases.append(dict(name='package-pair', passed=True))
        for name, invalid in (
                ('package-renderer-missing', rows[:1]),
                ('package-renderer-replaced', [rows[0], {**rows[1], 'source': str(args.without_moving_lights)}]),
                ('package-module-duplicate', rows + rows[:1])):
            try:
                capabilities.verify_renderer_rows(invalid, 'install/output/svencoop/')
            except ValueError as failure:
                cases.append(dict(name=name, passed=True, expectedRejection=str(failure)))
            else:
                raise AssertionError('Invalid package pair was accepted: ' + name)
        for name, renderer, rejected in (
                ('standalone-cli-old-renderer', args.without_moving_lights, True),
                ('standalone-cli-paired-renderer', args.with_moving_lights, False)):
            run = subprocess.run([sys.executable, '-X', 'utf8', '-B', str(spec.origin),
                                  '--candidate', str(args.with_refresh), '--renderer', str(renderer)],
                                 capture_output=True)
            assert bool(run.returncode) == rejected, name
            if not rejected:
                assert json.loads(run.stdout)['rendererCapabilities']['movingLightsRequired']
            cases.append(dict(name=name, passed=True, rejected=rejected))
    unchanged = all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == sha for path, sha in before.items())
    assert unchanged
    receipt = dict(success=True, inputsUnchanged=unchanged, cases=cases, inputs=before,
                   boundary='Actual failed and corrected DLL registration markers plus build configuration; no live provider or HUD rendering')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf8')
    print(json.dumps(dict(success=True, cases=len(cases), inputsUnchanged=unchanged)))


if __name__ == '__main__':
    main()
