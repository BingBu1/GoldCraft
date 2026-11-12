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


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('environment','commands','presentation','output'):
        parser.add_argument('--'+name,type=Path,required=True)
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
    sources=[ROOT/p for p in ('tests/cryoffear-host-admission/main.cpp',
        'native/csol/client/weapons/guest_cof_input.cpp','native/csol/client/weapons/guest_cof_admission.cpp',
        'native/csol/hud_style.cpp','native/csol/buy_menu.cpp')]
    inputs=set(sources)|{Path(__file__).resolve(),args.commands.resolve(),args.environment.resolve(),
                          config,compiler,compiler.with_name('lld-link.exe')}
    inputs.update(p.resolve() for p in args.presentation.rglob('*') if p.is_file())
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
        tested=subprocess.run([str(executable),str(args.presentation.resolve())],cwd=output,capture_output=True,text=True,timeout=30)
        (output/'test.log').write_text(tested.stdout+tested.stderr,'utf8')
    after={str(p):sha(p) for p in sorted(inputs)}
    success=not built.returncode and tested is not None and not tested.returncode and before==after
    receipt=dict(success=success,command=argv,before=before,after=after,unchanged=before==after,
        compiled=built.returncode,tested=tested.returncode if tested else None,
        sourceCopies=0,gameplay=False,performance=False,
        boundary='Actual SDK input/admission TUs and actual CSO presentation parsers; service readiness controlled; not actual UI/Renderer/network gameplay')
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf8')
    print(tested.stdout+tested.stderr if tested else built.stdout+built.stderr,flush=True)
    raise SystemExit(0 if success else 1)


if __name__=='__main__':
    main()
