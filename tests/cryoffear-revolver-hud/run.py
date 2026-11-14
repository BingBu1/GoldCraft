#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the real HUD and module registry in place; audit all compiler inputs."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('commands','environment','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();out=args.output.absolute()
    for path in (out,*out.parents):
        if path.is_symlink() or os.path.isjunction(path):raise ValueError('Linked output')
    if out.exists() or not out.resolve().is_relative_to(ROOT/'build'):
        raise ValueError('Fresh workspace output required')
    out.mkdir(parents=True)
    spec=importlib.util.spec_from_file_location('sdk',ROOT/'tests/csol-rebuy/run_sdk.py')
    sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
    rows=json.loads(args.commands.read_text('utf-8-sig'))
    row=next(r for r in rows if r['file'].replace('\\','/').endswith('/client/hud_style.cpp'))
    command=sdk.windows_arguments(row['command']);flags=[]
    for value in command[1:]:
        if value=='--':break
        if value in ('-c','/c') or value.startswith(('/Fo','/Fd')):continue
        flags.append(value)
    flags+=['/I'+str(ROOT),'/I'+str(ROOT/'native/csol'),'/I'+str(ROOT/'native/csol/client'),
            '/clang:-ffp-contract=off']
    env=dict(os.environ)
    for key,value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold()==key.casefold():del env[old]
        env[key]=value
    (out/'temp').mkdir();env.update(TEMP=str(out/'temp'),TMP=str(out/'temp'))
    units=['tests/cryoffear-revolver-hud/main.cpp','native/csol/hud_style.cpp','native/csol/image_codec.cpp',
           'native/csol/client/resource_font_memory.cpp','native/csol/client/weapons/weapon_module.cpp',
           'native/csol/client/weapons/guest_cof_hud.cpp','tests/csol-hud-style/pure.cpp',
           'native/csol/client/weapons/weapons.cpp','native/csol/client/weapons/guest_cof_admission.cpp']
    inputs={Path(__file__).resolve(),args.commands.resolve(),args.environment.resolve(),Path(command[0]).resolve(),
            ROOT/'tests/csol-rebuy/run_sdk.py'};jobs=[]
    for i,unit in enumerate(units):
        dep=out/(str(i)+'.d');job=[command[0],*flags,'/clang:-M','/clang:-MF'+str(dep),'/clang:-MTcheck',str(ROOT/unit)]
        result=subprocess.run(job,cwd=ROOT,env=env,capture_output=True,timeout=120)
        (out/(str(i)+'-headers.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError('Dependency discovery: '+unit)
        text=dep.read_text().replace('\\\n',' ')
        inputs.update(Path(p.replace('\\ ',' ')).resolve() for p in re.findall(r'(?:\\ |[^\s])+',text.split(':',1)[1]))
    before={str(p):sha(p) for p in sorted(inputs)}
    for i,unit in enumerate(units):
        job=[command[0],*flags,'/c',str(ROOT/unit),'/Fo'+str(out/(str(i)+'.obj'))]
        result=subprocess.run(job,cwd=ROOT,env=env,capture_output=True,timeout=180)
        (out/(str(i)+'-compile.log')).write_bytes(result.stdout+result.stderr)
        jobs.append(dict(unit=unit,command=job,exit=result.returncode))
        if result.returncode:break
    tested=[]
    if len(jobs)==len(units) and all(j['exit']==0 for j in jobs):
        for name,indices in (('hud',range(6)),('pure',(6,1))):
            exe=out/(name+'.exe')
            job=[command[0],'/nologo','/clang:-fuse-ld=lld',*[str(out/(str(i)+'.obj')) for i in indices],
                 '/Fe:'+str(exe),'/link','/machine:x86','/OPT:REF','/threads:1','user32.lib','gdi32.lib']
            result=subprocess.run(job,cwd=ROOT,env=env,capture_output=True,timeout=120)
            (out/(name+'-link.log')).write_bytes(result.stdout+result.stderr)
            jobs.append(dict(stage=name+'-link',command=job,exit=result.returncode))
            if result.returncode:break
            result=subprocess.run([str(exe)],cwd=ROOT,env=env,capture_output=True,timeout=60)
            (out/(name+'-test.log')).write_bytes(result.stdout+result.stderr)
            tested.append(dict(name=name,exit=result.returncode))
            print(result.stdout.decode('utf8','replace'),flush=True)
    after={str(p):sha(p) for p in sorted(inputs)}
    success=len(tested)==2 and all(t['exit']==0 for t in tested) and all(j['exit']==0 for j in jobs) and before==after
    receipt=dict(success=success,before=before,inputsUnchanged=before==after,jobs=jobs,tests=tested,sourceCopies=0,
        boundary='Real HUD/catalog/codec/static module registry and real native fallback/admission compilation; controlled surface/engine; no DLL, deployment, gameplay, GPU pixels or FPS acceptance')
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf-8')
    print(json.dumps(dict(success=success,inputs=len(inputs),tests=tested)))
    raise SystemExit(0 if success else 1)


if __name__=='__main__':main()
