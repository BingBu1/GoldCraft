#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual ReGameDLL SDK sender and test every packet callback edge."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
SDK=ROOT/'external/ReGameDLL_CS/regamedll'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('environment','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();output=args.output.absolute()
    if output.exists() or output==ROOT/'build' or not output.resolve().is_relative_to(ROOT/'build') or any(
            p.is_symlink() or os.path.isjunction(p) for p in (output,*output.parents)):
        raise ValueError('Use a fresh workspace build directory')
    output.mkdir(parents=True)
    config=ROOT/'.tools/clang-toolchain.json'
    compiler=Path(json.loads(config.read_text('utf-8-sig'))['root'])/'bin/clang-cl.exe'
    project=SDK/'msvc/ReGameDLL.vcxproj'
    includes=re.search(r'<AdditionalIncludeDirectories>(.*?)</AdditionalIncludeDirectories>',project.read_text('utf-8-sig'))[1].split(';')
    includes=[(project.parent/value[len('$(ProjectDir)'):].replace(chr(92),'/').lstrip('/')).resolve()
              for value in includes if value.startswith('$(ProjectDir)')]
    definitions='REGAMEDLL_ADD REGAMEDLL_API REGAMEDLL_FIXES REGAMEDLL_SSE REGAMEDLL_SELF REGAMEDLL_CHECKS UNICODE_FIXES BUILD_LATEST BUILD_LATEST_FIXES CLIENT_WEAPONS USE_QSTRING _CRT_SECURE_NO_WARNINGS NDEBUG'.split()
    flags=['/nologo','--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/utf-8',
           '/O2','/clang:-O3','/clang:-flto=thin','/clang:-fuse-ld=lld','/arch:AVX2',
           '/fp:precise','/clang:-ffp-contract=off','/Gy','/Gw',
           *('/D'+value for value in definitions),*('/I'+str(value) for value in includes),
           '/I'+str(ROOT),'/I'+str(ROOT/'native/include'),'/I'+str(output)]
    sources=[ROOT/p for p in ('tests/cryoffear-crosshair-sender/main.cpp',
        'native/guest/cof/server/crosshair_dispatch.cpp','native/guest/cof/crosshair_protocol.cpp')]
    inputs=set(sources)|{Path(__file__).resolve(),args.environment.resolve(),config,project,
                        compiler,compiler.with_name('lld-link.exe')}
    aborts=[]
    for name,classes in (('cbase.h',('CBaseEntity','CBaseDelay','CBaseAnimating','CBaseToggle')),
                        ('basemonster.h',('CBaseMonster',)),('player.h',('CBasePlayer',))):
        path=SDK/'dlls'/name;inputs.add(path);text=path.read_text('utf-8-sig')
        for cls in classes:
            match=re.search(r'class '+cls+r'\s*(?::[^{};]+)?\s*{',text);start=match.end();end=start;depth=1
            while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
            for signature in re.findall(r'virtual\s+([^;{}\n]+);',text[start:end-1]):
                signature=re.sub(r'\s*=\s*[^,)]+','',signature);signature=re.sub(r'\bEXPORT\b','',signature).strip()
                method=re.fullmatch(r'(.*?)([A-Za-z_][A-Za-z_0-9]*)(\s*\(.*)',signature)
                if not method:raise ValueError('SDK method not parsed: '+signature)
                aborts.append(method[1]+cls+'::'+method[2]+method[3]+' { std::abort(); }')
    adapter=output/'sdk-abort-virtuals.inc';adapter.write_text('\n'.join(aborts),'utf8');inputs.add(adapter)
    env=os.environ|json.loads(args.environment.read_text('utf-8-sig'))
    temp=output/'temp';temp.mkdir();env.update(TEMP=str(temp),TMP=str(temp))
    for i,source in enumerate(sources):
        dep=output/f'{i}.d'
        run=subprocess.run([str(compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),str(source)],
            cwd=output,env=env,capture_output=True,text=True,timeout=60)
        (output/f'{i}-headers.log').write_text(run.stdout+run.stderr,'utf8');run.check_returncode()
        make=dep.read_text().replace(chr(92)+chr(10),' ')
        inputs.update(Path(p.replace(chr(92)+' ',' ')).resolve() for p in re.findall(r'(?:\\ |[^\s])+',make.split(':',1)[1]))
    before={str(p):sha(p) for p in sorted(inputs)}
    executable=output/'crosshair-sender.exe'
    command=[str(compiler),*flags,*map(str,sources),'/Fe:'+str(executable),'/link','/machine:x86','/threads:1','/opt:ref']
    built=subprocess.run(command,cwd=output,env=env,capture_output=True,text=True,timeout=150)
    (output/'compile.log').write_text(built.stdout+built.stderr,'utf8')
    tested=None
    if not built.returncode:
        tested=subprocess.run([str(executable)],cwd=output,capture_output=True,text=True,timeout=30)
        (output/'test.log').write_text(tested.stdout+tested.stderr,'utf8')
    after={str(p):sha(p) for p in sorted(inputs)}
    success=not built.returncode and tested is not None and not tested.returncode and before==after
    receipt=dict(success=success,command=command,before=before,after=after,unchanged=before==after,
        compiled=built.returncode,tested=tested.returncode if tested else None,
        sourceCopies=0,gameplay=False,performance=False,
        boundary='Actual SDK sender, protocol and entity handles; every callback retirement/recycle boundary; actual outgoing engine scheduling/gameplay separate')
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf8')
    print(tested.stdout+tested.stderr if tested else built.stdout+built.stderr,flush=True)
    raise SystemExit(0 if success else 1)


if __name__=='__main__':main()
