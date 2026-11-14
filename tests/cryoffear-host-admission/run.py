#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build actual guest input/admission and parse the produced host presentation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
SOURCES=('tests/cryoffear-host-admission/main.cpp',
    'native/csol/client/weapons/guest_cof_input.cpp','native/csol/client/weapons/guest_cof_admission.cpp',
    'native/csol/hud_style.cpp','native/csol/buy_menu.cpp')
EXTRA_INPUTS=()
NEEDS_PRESENTATION=True
BOUNDARY='Actual SDK input/admission TUs and actual CSO presentation parsers; service readiness controlled; not actual UI/Renderer/network gameplay'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('environment','commands','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--presentation',type=Path,required=NEEDS_PRESENTATION)
    args=parser.parse_args();output=args.output.absolute()
    if output.exists() or output==ROOT/'build' or not output.resolve().is_relative_to(ROOT/'build') or any(
            p.is_symlink() or os.path.isjunction(p) for p in (output,*output.parents)):
        raise ValueError('Use a fresh workspace build directory')
    output.mkdir(parents=True)
    config=ROOT/'.tools/clang-toolchain.json'
    compiler=Path(json.loads(config.read_text('utf-8-sig'))['root'])/'bin/clang-cl.exe'
    database=json.loads(args.commands.read_text('utf8'))
    command=next(row['command'] for row in database if row['file'].replace(chr(92),'/').endswith('/native/csol/client/plugin.cpp'))
    tokens=re.findall(r'"[^"\r\n]*"|[^\s]+',command)
    flags=['/nologo','/TP','/std:c++20','/EHsc','/MT','/utf-8','/W3','/WX',
           '--target=i686-pc-windows-msvc','/O2','/clang:-O3','/clang:-flto=thin',
           '/arch:AVX2','/fp:precise','/clang:-ffp-contract=off','-Wno-overriding-option',
           '/DNDEBUG',*[s for s in tokens if s.startswith('-D')],
           *['/imsvc'+s[2:] for s in tokens if s.startswith('-I')]]
    flags.append('/I'+str(output))
    # Compile the real orchestration bodies; native input is an explicit overwrite boundary.
    chain_sources=[ROOT/'native/csol/client/plugin.cpp',ROOT/'native/csol/client/characters.cpp']
    def function(path, signature):
        text=path.read_text('utf-8-sig');start=text.index(signature);at=text.index('{',start);end=at+1;depth=1
        while depth:
            depth+=(text[end]=='{')-(text[end]=='}');end+=1
        return text[start:end]
    (output/'native-create-move-chain.inc').write_text(
        function(chain_sources[1],'void CharacterCreateMove(')+'\n'+
        function(chain_sources[0],'static void CreateMove(')+'\n','utf8')
    sources=[ROOT/p for p in SOURCES]
    inputs=set(sources)|set(chain_sources)|{Path(__file__).resolve(),args.commands.resolve(),args.environment.resolve(),
                          config,compiler,compiler.with_name('lld-link.exe')}
    inputs.update(ROOT/p for p in EXTRA_INPUTS)
    if args.presentation:
        root=args.presentation.resolve(strict=True)
        if not (root.is_relative_to(ROOT/'build') or root.is_relative_to(ROOT/'sandbox')) or any(
                p.is_symlink() or os.path.isjunction(p) for p in (root,*root.parents)):
            raise ValueError('Use an isolated presentation resource root')
        contract=ROOT/'native/csol/buy_menu.hpp'
        match=re.search(r'PresentationResource\[\]="(models/csol/buymenu_v\d+\.json)";',contract.read_text('utf8'))
        if not match:raise ValueError('Missing versioned buy-menu presentation contract')
        # These are the only files opened by this suite's actual parsers. Service
        # readiness is controlled here; scanning live configs/assets adds no proof.
        inputs.update((contract,(root/'models/csol/hud_v7.json').resolve(strict=True),
                       (root/match[1]).resolve(strict=True)))
    environment=os.environ|json.loads(args.environment.read_text('utf-8-sig'))
    temp=output/'temp';temp.mkdir();environment.update(TEMP=str(temp),TMP=str(temp))
    for i,source in enumerate(sources):
        dep=output/f'{i}.d'
        argv=[str(compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),str(source)]
        result=subprocess.run(argv,cwd=ROOT,env=environment,capture_output=True,text=True,timeout=90)
        (output/f'{i}-headers.log').write_text(result.stdout+result.stderr,'utf8');result.check_returncode()
        make=dep.read_text().replace(chr(92)+chr(10),' ')
        inputs.update(Path(p.replace(chr(92)+' ',' ')).resolve() for p in re.findall(r'(?:\\ |[^\s])+',make.split(':',1)[1]))
    before={str(p):sha(p) for p in sorted(inputs)}
    executable=output/'admission.exe'
    argv=[str(compiler),*flags,'/clang:-fuse-ld=lld',*map(str,sources),'/Fe:'+str(executable),
          '/link','/machine:x86','/threads:1','/opt:ref']
    built=subprocess.run(argv,cwd=output,env=environment,capture_output=True,text=True,timeout=150)
    (output/'compile.log').write_text(built.stdout+built.stderr,'utf8')
    tested=None
    if not built.returncode:
        arguments=[str(executable)]+([str(args.presentation.resolve())] if args.presentation else [])
        tested=subprocess.run(arguments,cwd=output,capture_output=True,text=True,timeout=30)
        (output/'test.log').write_text(tested.stdout+tested.stderr,'utf8')
    after={str(p):sha(p) for p in sorted(inputs)}
    success=not built.returncode and tested is not None and not tested.returncode and before==after
    receipt=dict(success=success,command=argv,before=before,after=after,unchanged=before==after,
        compiled=built.returncode,tested=tested.returncode if tested else None,
        sourceCopies=0,gameplay=False,performance=False,
        boundary=BOUNDARY)
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf8')
    print(tested.stdout+tested.stderr if tested else built.stdout+built.stderr,flush=True)
    raise SystemExit(0 if success else 1)


if __name__=='__main__':
    main()
