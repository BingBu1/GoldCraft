#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile actual HUD/respawn Init paths with separated engine and VGUI factories."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def record(path):
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proposal', type=Path, required=True)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--old-ref', required=True)
    parser.add_argument('--source-override', nargs=2, action='append', default=[],
                        metavar=('RELATIVE_SOURCE', 'FROZEN_SOURCE'),
                        help='Test a reviewed frozen production source without editing the working tree')
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use a new workspace build output')
    out.mkdir(parents=True)
    plan = json.loads(args.proposal.read_text('utf-8-sig'))
    env = os.environ | json.loads(Path(plan['environment']['path']).read_text('utf-8-sig'))
    compile_args = next(j['command'] for j in plan['jobs'] if j['name'] == 'compile-client')
    flags, skip = [], False
    for value in compile_args[1:]:
        if value in ['-c', '--'] or skip:
            if value == '--': break
            skip = False
            continue
        if value == '/clang:-ivfsoverlay': skip = True; continue
        if value.startswith(('/Fo', '/Fd')) or ('\\build\\' in value and value.startswith('-I')):
            continue
        flags.append(value)
    roots = ['tests/csol-hud-style/interfaces.cpp', 'tests/csol-scenario-respawn-ui/interfaces.cpp',
             'native/csol/hud_style.cpp', 'native/csol/image_codec.cpp', 'native/csol/scenario/core/scenario_localization.cpp']
    frozen = {}
    overrides = {}
    for name, filename in args.source_override:
        logical, source = (ROOT / name).resolve(), Path(filename).resolve()
        if not logical.is_relative_to(ROOT / 'native') or not source.is_relative_to(ROOT / 'build'):
            raise ValueError('Overrides must map native source to a workspace build snapshot')
        if logical in overrides or not source.is_file():
            raise ValueError('Duplicate or missing source override')
        overrides[logical] = source
    pending = [ROOT / name for name in roots]
    while pending:
        path = pending.pop().resolve()
        if path in frozen: continue
        if not path.is_relative_to(ROOT): raise ValueError('Source outside workspace')
        source = overrides.get(path, path)
        data = source.read_bytes()
        target = out / 'source' / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
        frozen[path] = dict(logicalPath=str(path), original=record(source), frozen=record(target))
        for name in re.findall(rb'^\s*#\s*include\s*"([^"\r\n]+)"', data, re.M):
            name = name.decode()
            found = next((p for p in [path.parent / name, ROOT / name] if p.is_file()), None)
            if found: pending.append(found)
    if not overrides.keys() <= frozen.keys():
        raise ValueError('Source override was not part of the compiled closure')
    overlay = dict(version=0, **{'case-sensitive': False, 'use-external-names': False}, roots=[
        {'type': 'file', 'name': str(p), 'external-contents': v['frozen']['path']} for p, v in frozen.items()])
    (out / 'overlay.json').write_text(json.dumps(overlay), encoding='utf8')
    old = json.loads(json.dumps(overlay))
    for name in ['native/csol/client/hud_style.cpp', 'native/csol/client/scenario/core/scenario_respawn_ui.cpp']:
        data = subprocess.check_output(['git', 'show', args.old_ref + ':' + name], cwd=ROOT)
        target = out / 'old' / name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
        next(row for row in old['roots'] if Path(row['name']) == ROOT / name)['external-contents'] = str(target)
    (out / 'old-overlay.json').write_text(json.dumps(old), encoding='utf8')
    jobs = []
    def execute(name, argv, expected=0):
        started = time.monotonic()
        proc = subprocess.run(argv, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        text = proc.stdout.decode('utf8', 'replace'); (out / (name + '.log')).write_text(text, encoding='utf8')
        row = dict(name=name, command=argv, exitCode=proc.returncode, expectedExit=expected,
                   seconds=time.monotonic()-started, log=record(out / (name + '.log')))
        jobs.append(row)
        (out / 'jobs.json').write_text(json.dumps(jobs, indent=2), encoding='utf8')
        if proc.returncode != expected:
            print(text[-5000:]); raise RuntimeError(name + ' failed')
        print(name, 'exit', proc.returncode, flush=True)
    def compile_one(name, path, previous=False):
        ov=out / ('old-overlay.json' if previous else 'overlay.json')
        execute('compile-'+name, [compile_args[0], *flags, '-I'+str(ROOT), '/clang:-ivfsoverlay', '/clang:'+str(ov),
            '/Fo'+str(out/(name+'.obj')), '/Fd'+str(out/(name+'.compile.pdb')), '-c', '--', str(ROOT/path)])
    for name,path in [('style',roots[2]),('codec',roots[3]),('localization',roots[4])]: compile_one(name,path)
    link_base = next(j['command'] for j in plan['jobs'] if j['name'] == 'link-client')
    link_flags = [x for x in link_base[1:] if not x.startswith(('/out:', '/pdb:')) and not x.endswith('.obj')]
    for previous in [False, True]:
        for label,path,objects in [('hud',roots[0],['style','codec']),('respawn',roots[1],['localization','codec'])]:
            name=label+('-old' if previous else '-new');compile_one(name,path,previous)
            execute('link-'+name,[link_base[0],*link_flags,'/out:'+str(out/(name+'.exe')),
                '/pdb:'+str(out/(name+'.pdb')),*[str(out/(x+'.obj')) for x in [name,*objects]]])
            argv=[str(out/(name+'.exe'))]+([str(args.resources.resolve())] if label=='respawn' else [])
            execute('run-'+name,argv,1 if previous else 0)
    unchanged=all(record(Path(v['original']['path']))['sha256']==v['original']['sha256'] for v in frozen.values())
    result=dict(success=unchanged,inputsUnchanged=unchanged,source=list(frozen.values()),jobs=jobs,
        compiledFullDll=False,liveGameplay=False,oldRef=args.old_ref,
        boundary='Full production Init/Prepare/Paint with real SDK types and TGA decoder; engine/VGUI factories and font handles controlled. No GPU pixels or native scoreboard tree.')
    (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    if not unchanged: raise RuntimeError('Sources changed during test')
    print('RESULT',out/'result.json')


if __name__ == '__main__':
    main()
