# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the real stage service with actual ReHLDS SDK and native Think bodies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
SDK=ROOT/'external/ReHLDS/rehlds'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def body(text,signature):
    start=text.index(signature);at=text.index('{',start);end=at+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('compiler','environment','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.absolute()
    for path in (out,*out.parents):
        if path.is_symlink() or os.path.isjunction(path):raise ValueError('Linked output')
    if out.exists() or not out.resolve().is_relative_to(ROOT/'build'):raise ValueError('Fresh workspace output required')
    out.mkdir(parents=True)
    project=SDK/'msvc/ReHLDS.vcxproj';native=SDK/'engine/sv_user.cpp'
    text=native.read_text('utf-8-sig')
    lifecycle={}
    for path,signature,lastConsumer in ((SDK/'engine/host.cpp','void Host_ShutdownServer(','FreeAllEntPrivateData();'),
            (SDK/'engine/host.cpp','void Host_ClearMemory(','SV_ClearCaches();'),
            (SDK/'engine/sys_dll.cpp','void ReleaseEntityDlls(','gNewDLLFunctions.pfnGameShutdown();')):
        value=body(path.read_text('utf-8-sig'),signature)
        if value.count('GoldCraftPlayerThinkStageReset();')!=2 or value.rindex('GoldCraftPlayerThinkStageReset();')<value.index(lastConsumer):
            raise ValueError('Stage callback survives native teardown: '+signature)
        if signature.startswith('void ReleaseEntityDlls') and value.rindex('GoldCraftPlayerThinkStageReset();')>value.index('FreeLibrary('):
            raise ValueError('Stage reset after module unload')
        lifecycle[signature]=dict(bytes=len(value.encode()),sha256=hashlib.sha256(value.encode()).hexdigest())
    functions='\n'.join(body(text,sig) for sig in ('void SV_PlayerRunPreThink(', 'qboolean SV_PlayerRunThink('))
    start=text.index('\tconst std::uint32_t thinkStageMap =');end=text.index('\n\tif (Length(',start)
    prefix=text[start:end]
    if not start<text.index('\tgEntityInterface.pfnPM_Move',start):raise ValueError('Incorrect stage order')
    (out/'native-player-think.inc').write_text(functions,'utf-8')
    (out/'native-usercmd-think.inc').write_text(prefix,'utf-8')
    sources=[ROOT/'native/engine/player_think_stage.cpp',Path(__file__).with_name('main.cpp'),SDK/'public/interface.cpp']
    inputs={Path(__file__).resolve(),a.compiler.resolve(),a.environment.resolve(),native,project,
            SDK/'engine/host.cpp',SDK/'engine/sys_dll.cpp',*sources,*out.glob('*.inc')}
    includes=re.search(r'<AdditionalIncludeDirectories>(.*?)</AdditionalIncludeDirectories>',project.read_text('utf-8-sig'))[1].split(';')
    includes=[(project.parent/v[len('$(ProjectDir)'):].replace('\\','/').lstrip('/')).resolve() for v in includes if v.startswith('$(ProjectDir)')]
    includes.extend((ROOT/'native/engine',ROOT/'native/include',ROOT/'external/ReHLDS/dep/bzip2/include',ROOT/'external/ReHLDS/dep/cppunitlite/include',out))
    defs='NOMINMAX REHLDS_API REHLDS_FIXES REHLDS_SSE REHLDS_OPT_PEDANTIC REHLDS_SELF REHLDS_CHECKS HAVE_OPT_STRTOOLS USE_BREAKPAD_HANDLER DEDICATED SWDS _CRT_SECURE_NO_WARNINGS NDEBUG'.split()
    flags=['/nologo','/FIprecompiled.h','--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/utf-8','/O2','/clang:-O3',
           '/clang:-flto=thin','/clang:-fuse-ld=lld','/arch:AVX2','/fp:precise','/clang:-ffp-contract=off','/Gy','/Gw',
           *('/D'+v for v in defs),*('/I'+str(v) for v in includes)]
    env=dict(os.environ)
    for key,value in json.loads(a.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold()==key.casefold():del env[old]
        env[key]=value
    (out/'temp').mkdir();env.update(TEMP=str(out/'temp'),TMP=str(out/'temp'))
    commands=[]
    for n,source in enumerate(sources):
        dep=out/f'{n}.d';command=[str(a.compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),str(source)]
        run=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True,timeout=60)
        (out/f'{n}-headers.log').write_text(run.stdout+run.stderr,'utf-8');commands.append(command)
        if run.returncode:raise RuntimeError('Dependency discovery failed: '+str(source))
        make=dep.read_text().replace(chr(92)+chr(10),' ')
        inputs.update(Path(v.replace(chr(92)+' ',' ')).resolve() for v in re.findall(r'(?:\\ |[^\s])+',make.split(':',1)[1]))
    before={str(v):sha(v) for v in sorted(inputs)}
    exe=out/'stage.exe';command=[str(a.compiler),*flags,*map(str,sources),'/Fe:'+str(exe),'/link','/machine:x86','/threads:1','/opt:ref']
    compiled=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True,timeout=120)
    (out/'compile.log').write_text(compiled.stdout+compiled.stderr,'utf-8');tested=None
    if not compiled.returncode:
        tested=subprocess.run([str(exe)],cwd=out,capture_output=True,text=True,timeout=30)
        (out/'test.log').write_text(tested.stdout+tested.stderr,'utf-8')
    after={str(v):sha(v) for v in sorted(inputs)}
    success=not compiled.returncode and tested is not None and not tested.returncode and before==after
    (out/'receipt.json').write_text(json.dumps(dict(success=success,command=command,dependencyCommands=commands,
        dependencyBefore=before,dependencyAfter=after,compiled=compiled.returncode,tested=tested.returncode if tested else None,
        lifecycle=lifecycle,sourceCopies=0,deployed=False,gameplay=False,performance=False,
        boundary='Real ReHLDS SDK, interface registry and complete PreThink/Think bodies plus exact usercmd prefix; controlled native callback/free services, no live engine initialization'),indent=2),'utf-8')
    print(tested.stdout+tested.stderr if tested else compiled.stdout+compiled.stderr)
    raise SystemExit(0 if success else 1)
if __name__=='__main__':main()
