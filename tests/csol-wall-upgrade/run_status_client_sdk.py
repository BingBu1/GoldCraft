#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual status client with SDK, localization parser and VGUI boundary."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]

def identity(path):
    path = Path(path).resolve(strict=True)
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--old', type=Path)
    parser.add_argument('--status-source', type=Path,
                        help='Replace only the status consumer for a preserved regression candidate.')
    parser.add_argument('--expect-failure', action='store_true')
    parser.add_argument('--baseline-manifest', type=Path,
                        help='Reuse supporting production sources from the complete client build.')
    parser.add_argument('--localization-source', type=Path)
    parser.add_argument('--hud-source', type=Path)
    parser.add_argument('--scheme', type=Path, required=True)
    parser.add_argument('--font-first', action='store_true')
    parser.add_argument('--notice-route', action='store_true')
    parser.add_argument('--without-resource-v4', action='store_true', default=True,
                        help='Use the deployed baseline without the pending resource-v4 read gate.')
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists()
    for p in (out, *out.parents):
        assert not p.is_junction() and not p.is_symlink(), p
    previous = ROOT/'build/csol-oberon-hint-20261031/client-validation-01'
    plan = json.loads((previous/'build-proposal.json').read_text('utf-8'))
    out.mkdir()
    inputs, sources = [], []
    baseline = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    support = {}
    if args.baseline_manifest:
        inputs.append(identity(args.baseline_manifest))
        support = {row['relative']:row['frozen'] for row in
                   json.loads(args.baseline_manifest.read_text('utf-8'))['files']}
    candidates = {'native/csol/scenario/core/wall_status.hpp', 'native/csol/client/scenario/core/wall_status.cpp',
                  'native/csol/client/scenario/core/wall_upgrade_hud.hpp', 'native/csol/client/scenario/core/wall_upgrade_hud.cpp'}
    relatives = ('tests/csol-wall-upgrade/status_client.cpp', 'tests/csol-wall-upgrade/hud_interfaces.cpp',
        'tests/csol-wall-status/surface_stub.hpp',
        'native/csol/scenario/core/wall_status.hpp', 'native/csol/client/scenario/core/wall_status.hpp',
        'native/csol/client/scenario/core/wall_status.cpp', 'native/csol/client/runtime.hpp',
        'native/csol/client/scenario/core/wall_upgrade_hud.hpp', 'native/csol/client/scenario/core/wall_upgrade_hud.cpp',
        'native/csol/client/scenario/core/scenario_round_hud_client.hpp', 'native/csol/scenario/core/scenario_round_hud.hpp',
        'native/csol/scenario/core/message_table.hpp', 'native/csol/scenario/core/config.hpp',
        'native/csol/scenario/core/spawn_points.hpp',
        'native/csol/image_codec.hpp', 'native/csol/image_codec.cpp',
        'native/csol/client/resource_refresh.hpp', 'native/csol/client/overlay_layers.hpp',
        'native/csol/client/user_message_chain.hpp', 'native/include/goldcraft/csol_resource_api.hpp',
        'native/csol/scenario/core/scenario_localization.hpp', 'native/csol/scenario/core/scenario_localization.cpp')
    if args.notice_route:
        relatives += ('tests/csol-wall-upgrade/notice_route.cpp',
            'native/csol/client/scenario/core/scenario_round_hud_client.cpp',
            'native/csol/scenario/core/scenario_round_hud.cpp', 'native/csol/scenario/core/dialogue_resources.hpp')
    for relative in relatives:
        if args.without_resource_v4 and relative in (
                'native/csol/client/resource_refresh.hpp',
                'native/include/goldcraft/csol_resource_api.hpp'):
            continue
        source, target = ROOT/relative, out/'source'/relative
        if relative in support and relative not in candidates:
            source = Path(support[relative]['path'])
            assert identity(source) == support[relative]
        if args.old and relative in ('native/csol/scenario/core/wall_status.hpp', 'native/csol/client/scenario/core/wall_status.cpp'):
            source = args.old.resolve()/relative
        if args.status_source and relative == 'native/csol/client/scenario/core/wall_status.cpp':
            source = args.status_source.resolve()
        if args.localization_source and relative == 'native/csol/scenario/core/scenario_localization.cpp':
            source = args.localization_source.resolve()
        if args.hud_source and relative == 'native/csol/client/scenario/core/wall_upgrade_hud.cpp':
            source = args.hud_source.resolve()
        before = identity(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        content = source.read_bytes()
        if args.without_resource_v4 and relative == 'native/csol/client/overlay_layers.hpp' and relative not in support:
            content = subprocess.check_output(['git','show',baseline+':'+relative],cwd=ROOT)
        if args.without_resource_v4 and relative == 'native/csol/client/scenario/core/wall_status.cpp':
            text = content.decode('utf-8')
            if 'ResourceContentReadsAllowed()' in text:
                assert text.count('ResourceContentReadsAllowed() && ') == 1
                text = text.replace('#include "resource_refresh.hpp"\r\n', '').replace('#include "resource_refresh.hpp"\n', '')
                text = text.replace('ResourceContentReadsAllowed() && ', '')
                content = text.encode('utf-8')
        target.write_bytes(content)
        assert identity(source) == before and target.stat().st_nlink == 1
        inputs.append(identity(target))
        sources.append(dict(relative=relative, source=before, frozen=inputs[-1]))
    runner = out/'run.py'
    runner.write_bytes((previous/'run.py').read_bytes())
    inputs += [identity(runner), identity(__file__), identity(previous/'build-proposal.json')]
    job = plan['jobs'][0]
    argv = []
    for token in job['compile']:
        if token.endswith('tests\\csol-oberon-hint\\main.cpp'):
            token = str(out/'source/tests/csol-wall-upgrade'/('notice_route.cpp' if args.notice_route else 'status_client.cpp'))
        elif token.endswith('native\\csol\\client\\oberon_hint.cpp'):
            token = str(out/'source/native/csol/scenario/core/scenario_round_hud.cpp' if args.notice_route else out/'source/native/csol/client/scenario/core/wall_status.cpp')
        elif token.startswith('/Fo'):
            token = '/Fo'+str(out/'actual-client')+'/'
        elif token.startswith('/Fe'):
            token = '/Fe'+str(out/'actual-client/tests.exe')
        argv.append(token)
    argv[argv.index('/link'):argv.index('/link')] = ['/clang:-ffp-contract=off',
        str(out/'source/native/csol/scenario/core/scenario_localization.cpp'),
        str(out/'source/tests/csol-wall-upgrade/hud_interfaces.cpp'),
        str(out/'source/native/csol/image_codec.cpp')]
    argv.append('gdi32.lib')
    inputs.append(identity(argv[0]))
    font = ROOT/'sandbox/cs-client-b/Half-Life/cstrike/resource/font/df_gb_y9.ttf'
    inputs.append(identity(font))
    catalog = ROOT/'sandbox/cs-client-b/Half-Life/cstrike_downloads/resource/cso_chn.txt'
    inputs.append(identity(catalog))
    inputs.append(identity(args.scheme))
    job.update(directory=str(out/'actual-client'), compile=argv,
               testArguments=[str(font),str(catalog),str(args.scheme.resolve()),'font-first' if args.font_first else 'all'],
               executable=str(out/'actual-client/tests.exe'), compileSeconds=180,
               testExit=1 if args.old or args.expect_failure else 0,
               expectedSummary=('wall route check line' if args.notice_route else 'wall status check line') if args.old or args.expect_failure else
               ('wall route production consumer checks:' if args.notice_route else 'wall status production consumer checks:'))
    plan.update(inputs=inputs, sources=sources, withoutResourceV4=args.without_resource_v4,
                baselineCommit=baseline,
                command=[sys.executable,str(runner),'--execute-authorized-plan'],
                boundary=('Actual RoundHUD Receive/Show/State/NoticeAlpha and wall notice consumer; '
                          'downstream effects dispatcher and absent graphics surface are controlled boundaries. '
                          'No complete plugin or gameplay.' if args.notice_route else
                          'Actual wall status/HUD consumers, localization parser and fixed-width state; '
                          'real frozen x86 SDK. Engine hooks/VGUI resource operations controlled. '
                          'Old source must fail the same fixture; no complete plugin or gameplay.'))
    (out/'build-proposal.json').write_text(json.dumps(plan, indent=2), 'utf-8')
    raise SystemExit(subprocess.call(plan['command'],cwd=ROOT))

if __name__ == '__main__':
    main()
