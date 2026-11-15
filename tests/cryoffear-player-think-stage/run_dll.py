# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the named player Think service in a sealed complete engine module."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[2]
def identity(p):return dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest(),bytes=p.stat().st_size)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('compiler','environment','engine','steam-api','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'build'):raise ValueError('Fresh workspace output required')
    for path in (out,*out.parents):
        if path.is_symlink() or os.path.isjunction(path):raise ValueError('Linked output')
    for path in (a.engine,a.steam_api):
        if not path.resolve(strict=True).is_relative_to(ROOT):raise ValueError('Workspace runtime inputs only')
    out.mkdir(parents=True);source=Path(__file__).with_name('dll.cpp')
    paths=[Path(__file__),source,a.compiler,a.environment,a.engine,a.steam_api,
           ROOT/'native/include/goldcraft/player_think_stage_api.hpp',ROOT/'external/ReHLDS/rehlds/public/interface.h']
    before=[identity(v) for v in paths];env=dict(os.environ)
    for key,value in json.loads(a.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold()==key.casefold():del env[old]
        env[key]=value
    for src,name in ((a.engine,'swds.dll'),(a.steam_api,'steam_api.dll')):
        dest=out/name;shutil.copy2(src,dest)
        if identity(src)['sha256']!=identity(dest)['sha256'] or dest.stat().st_nlink!=1:raise ValueError('Sealed module copy mismatch')
    exe=out/'stage-factory.exe'
    command=[str(a.compiler.resolve()),'/nologo','--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/DNOMINMAX',
        '/arch:AVX2','/fp:precise','/clang:-ffp-contract=off','/clang:-O3','/clang:-flto=thin','-fuse-ld=lld',
        '/I'+str(ROOT/'native/include'),'/I'+str(ROOT/'external/ReHLDS/rehlds/public'),str(source),
        '/Fo'+str(out/'dll.obj'),'/Fe'+str(exe),'/link','/machine:x86','/threads:1']
    steps=[]
    for name,cmd in (('compile',command),('load',[str(exe),str(out/'swds.dll')])):
        run=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,timeout=120)
        (out/(name+'.log')).write_text(run.stdout+run.stderr,'utf-8');steps.append(dict(name=name,command=cmd,exit=run.returncode))
        print(name,run.returncode,(run.stdout+run.stderr)[-2000:],flush=True)
        if run.returncode:break
    stable=before==[identity(v) for v in paths];success=len(steps)==2 and all(not v['exit'] for v in steps) and stable
    (out/'receipt.json').write_text(json.dumps(dict(success=success,inputsUnchanged=stable,inputs=before,steps=steps,
        deployed=False,gameplay=False,boundary='Real complete x86 engine factory and API; no server initialization or task execution'),indent=2),'utf-8')
    raise SystemExit(0 if success else 1)
if __name__=='__main__':main()
