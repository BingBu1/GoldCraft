#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build current in-place ReHLDS sources using an existing isolated output cache."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT=Path(__file__).resolve().parent.parent
def identity(path):
    path=Path(path).resolve(strict=True)
    return dict(path=str(path),bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','output','environment','compiler-dir','msbuild'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--jobs',type=int,default=2,choices=range(1,17))
    p.add_argument('--windows-sdk',default='10.0.26100.0')
    p.add_argument('--steam-approval', action='store_true',
                   help='Expose verified Steam identities to GameDLL persistence')
    a=p.parse_args();cache=a.build.resolve(strict=True);out=a.output.resolve()
    if not cache.is_relative_to(ROOT/'build') or not out.is_relative_to(ROOT/'build') or out.exists():
        raise ValueError('Existing workspace cache and fresh receipt directory required')
    for path in (cache,out,*out.parents):
        if path.is_symlink() or os.path.isjunction(path):raise ValueError('Linked build path')
    out.mkdir(parents=True)
    spec=importlib.util.spec_from_file_location('checkpoint_guard',ROOT/'tools/Build-CSOLCandidate.py')
    guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
    controls=[Path(__file__),ROOT/'tools/Build-CSOLCandidate.py',a.environment,a.msbuild,cache/'Build-Isolated.targets',
              ROOT/'native/engine/GoldCraftEngine.targets',ROOT/'cmake/GoldCraftClang.targets',
              *(a.compiler_dir/name for name in ('clang-cl.exe','lld-link.exe','llvm-lib.exe'))]
    before=[identity(v) for v in controls];guard.begin(out/'sources')
    env=dict(os.environ)
    for key,value in json.loads(a.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold()==key.casefold():del env[old]
        env[key]=value
    compiler=str(a.compiler_dir.resolve(strict=True))+'/'
    command=[str(a.msbuild.resolve(strict=True)),str(ROOT/'external/ReHLDS/msvc/ReHLDS.sln'),
        '/m:'+str(a.jobs),'/nologo','/v:normal','/t:ReHLDS,dedicated,filesystem_stdio',
        '/p:Configuration=Release','/p:Platform=Win32','/p:PlatformToolset=v145',
        '/p:PreferredToolArchitecture=x64','/p:WindowsTargetPlatformVersion='+a.windows_sdk,
        '/p:VcpkgEnabled=false','/p:CLToolPath='+compiler,'/p:CLToolExe=clang-cl.exe',
        '/p:LinkToolPath='+compiler,'/p:LinkToolExe=lld-link.exe','/p:LibToolPath='+compiler,
        '/p:LibToolExe=llvm-lib.exe','/p:WholeProgramOptimization=false',
        '/p:ForceImportBeforeCppTargets='+str(ROOT/'native/engine/GoldCraftEngine.targets'),
        '/p:ForceImportAfterCppTargets='+str(cache/'Build-Isolated.targets'),'/p:GoldCraftRoot='+str(ROOT),
        '/p:GoldCraftResourceSyncV4=true','/p:OutDir='+str(cache)+'/',
        '/p:GoldCraftSteamApproval='+str(a.steam_approval).lower(),
        '/p:PreBuildEventUseInBuild=false','/p:PostBuildEventUseInBuild=false','/p:MultiProcessorCompilation=false',
        '/p:UseMultiToolTask=true','/p:EnforceProcessCountAcrossBuilds=true',
        '/p:MultiProcMaxCount='+str(a.jobs),'/p:CL_MPCount='+str(a.jobs),'/nr:false']
    for name in ('swds.dll','swds.pdb','hlds.exe','filesystem_stdio.dll'):
        path=cache/name
        if path.exists() and path.stat().st_nlink!=1:raise ValueError('Shared compiler output: '+name)
    started=time.monotonic()
    with (out/'msbuild.log').open('w',encoding='utf-8') as log:
        run=subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
    stable=True;error=None
    try:
        guard.check_sources(out/'sources')
        if before!=[identity(v) for v in controls]:raise ValueError('Build controls changed')
    except Exception as e:stable=False;error=str(e)
    success=not run.returncode and stable;sealed=[]
    if success:
        (out/'frozen').mkdir()
        for name in ('swds.dll','swds.pdb','hlds.exe','filesystem_stdio.dll'):
            source=cache/name;dest=out/'frozen'/name;shutil.copy2(source,dest)
            if identity(source)['sha256']!=identity(dest)['sha256'] or dest.stat().st_nlink!=1:
                raise ValueError('Independent sealed artifact mismatch: '+name)
            sealed.append(identity(dest))
    receipt=dict(success=success,exit=run.returncode,inputsUnchanged=stable,error=error,controls=before,
        command=command,elapsedSeconds=time.monotonic()-started,sourceCopies=0,sealed=sealed,
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        processBudget='shared',jobs=a.jobs,nodeReuse=False,deployed=False,gameplay=False,
        validation='Full in-place ReHLDS build and stable broad source inventory; no server/gameplay/performance acceptance')
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf-8')
    print(json.dumps(dict(success=success,exit=run.returncode,inputsUnchanged=stable,sealed=sealed)))
    raise SystemExit(0 if success else 1)
if __name__=='__main__':main()
