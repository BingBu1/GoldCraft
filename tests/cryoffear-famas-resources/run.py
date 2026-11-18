# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile real FAMAS cold-resource code and exercise actual SDK callbacks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[2]
SDK=ROOT/'external/ReGameDLL_CS/regamedll'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sample','proof','original-game-root','inherited-game-root','compiler','environment','output'):
        p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();output=a.output.absolute()
    for path in (output,*output.parents):
        if path.is_symlink() or os.path.isjunction(path):raise ValueError('Linked output')
    if output.exists() or not output.resolve().is_relative_to(ROOT/'build'):raise ValueError('Use fresh workspace output')
    proof=json.loads(a.proof.read_text('utf-8'))
    if not proof['beforeImplementation'] or sha(a.sample)!=proof['sampleSha256']:raise ValueError('Original identity differs')
    pe=a.sample.read_bytes();nt=struct.unpack_from('<I',pe,60)[0];opt=nt+24
    if pe[nt:nt+4]!=b'PE\0\0' or struct.unpack_from('<H',pe,nt+4)[0]!=0x14c:raise ValueError('Expected PE32 x86')
    base=struct.unpack_from('<I',pe,opt+28)[0];tab=opt+struct.unpack_from('<H',pe,nt+20)[0]
    inputs={Path(__file__).resolve(),a.sample.resolve(),a.proof.resolve(),a.environment.resolve()}
    for row in proof['regions']:
        rva=int(row['va'],16)-base;found=False
        for i in range(struct.unpack_from('<H',pe,nt+6)[0]):
            virtual,size,raw=struct.unpack_from('<III',pe,tab+40*i+12)
            if virtual<=rva and rva+row['size']<=virtual+size:
                start=raw+rva-virtual;found=hashlib.sha256(pe[start:start+row['size']]).hexdigest()==row['sha256'];break
        if not found:raise ValueError('Original IDA bytes differ')
    for name,digest in proof['archives'].items():
        path=a.proof.parent/name;inputs.add(path)
        if sha(path)!=digest:raise ValueError('Original IDA archive differs')
    for row in proof['resources']:
        path=a.original_game_root/row['original']
        if row['exists']:
            if sha(path)!=row['sha256']:raise ValueError('Original resource differs')
            inputs.add(path.resolve())
        elif path.exists():raise ValueError('Originally absent source appeared; re-audit')
    inherited=a.inherited_game_root/'models/w_9mmclip.mdl';inputs.add(inherited.resolve())
    # Identity pinned by the existing reviewed resource pack, including its
    # explicit inherited-root rule; never substitute a namesake file.
    import importlib.util
    tool=ROOT/'tools/Prepare-CryOfFearResourcePack.py';inputs.add(tool)
    spec=importlib.util.spec_from_file_location('cof_famas_resource_pins',tool)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    clip=next(row for row in module.PINS if row['original']=='models/w_9mmclip.mdl')
    if sha(inherited)!=clip['sha256']:raise ValueError('Inherited clip identity differs')
    sources=[ROOT/'tests/cryoffear-famas-resources/main.cpp',ROOT/'native/csol/server/weapons/guest_cof_resources.cpp',
             ROOT/'native/guest/cof/magazine_model.cpp']
    project=SDK/'msvc/ReGameDLL.vcxproj';inputs.update(sources);inputs.add(project)
    includes=re.search(r'<AdditionalIncludeDirectories>(.*?)</AdditionalIncludeDirectories>',project.read_text('utf-8-sig'))[1].split(';')
    includes=[(project.parent/v[len('$(ProjectDir)'):].replace('\\','/').lstrip('/')).resolve() for v in includes if v.startswith('$(ProjectDir)')]
    defs='REGAMEDLL_ADD REGAMEDLL_API REGAMEDLL_FIXES REGAMEDLL_SSE REGAMEDLL_SELF REGAMEDLL_CHECKS UNICODE_FIXES BUILD_LATEST BUILD_LATEST_FIXES CLIENT_WEAPONS USE_QSTRING _CRT_SECURE_NO_WARNINGS NDEBUG'.split()
    flags=['/nologo','--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/utf-8','/O2','/clang:-O3','/clang:-flto=thin',
        '/clang:-fuse-ld=lld','/arch:AVX2','/fp:precise','/clang:-ffp-contract=off','/Gy','/Gw',
        *('/D'+v for v in defs),*('/I'+str(v) for v in includes),'/I'+str(ROOT)]
    output.mkdir(parents=True);temporary=output/'temporary';temporary.mkdir()
    env=os.environ|json.loads(a.environment.read_text('utf-8-sig'));env.update(TEMP=str(temporary),TMP=str(temporary))
    dependencies=set(inputs);commands=[]
    for i,source in enumerate(sources):
        dep=output/f'{i}.d';command=[str(a.compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),str(source)]
        commands.append(command);run=subprocess.run(command,env=env,cwd=output,capture_output=True,text=True,timeout=60)
        (output/f'{i}-headers.log').write_text(run.stdout+run.stderr,'utf-8')
        if run.returncode:raise RuntimeError('SDK dependency discovery failed')
        make=dep.read_text().replace(chr(92)+chr(10),' ')
        dependencies.update(Path(v.replace(chr(92)+' ',' ')).resolve() for v in re.findall(r'(?:\\ |[^\s])+',make.split(':',1)[1]))
    git_before=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    before={str(v):sha(v) for v in sorted(dependencies)};compiler=sha(a.compiler)
    exe=output/'resources.exe';command=[str(a.compiler),*flags,*map(str,sources),'/Fe:'+str(exe),'/link','/machine:x86','/threads:1','/opt:ref']
    compiled=subprocess.run(command,env=env,cwd=output,capture_output=True,text=True,timeout=120)
    (output/'compile.log').write_text(compiled.stdout+compiled.stderr,'utf-8');tested=None
    if not compiled.returncode:
        tested=subprocess.run([str(exe),str(a.original_game_root.resolve()),str(a.inherited_game_root.resolve())],cwd=output,capture_output=True,text=True,timeout=30)
        (output/'test.log').write_text(tested.stdout+tested.stderr,'utf-8')
    after={str(v):sha(v) for v in sorted(dependencies)}
    success=not compiled.returncode and tested is not None and not tested.returncode and before==after and compiler==sha(a.compiler)
    git_after=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    receipt=dict(success=success,compiled=compiled.returncode,tested=tested.returncode if tested else None,gitBefore=git_before,gitAfter=git_after,
        dependencyBefore=before,dependencyAfter=after,command=command,dependencyCommands=commands,compilerSha256=compiler,
        original=dict(sampleSha256=proof['sampleSha256'],proofSha256=sha(a.proof),regions=len(proof['regions'])),
        boundary='Actual resource helper/SDK types, controlled engine VFS/registration; no authored P/client effects/production installation/gameplay or live performance',sourceCopies=0)
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf-8')
    print(tested.stdout+tested.stderr if tested else compiled.stdout+compiled.stderr)
    raise SystemExit(0 if success else 1)
if __name__=='__main__':main()
