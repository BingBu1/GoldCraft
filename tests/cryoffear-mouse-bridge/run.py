#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build and execute the actual matched native mouse detour and lifecycle."""
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
    for name in ('environment','commands','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--without-resource-refresh',action='store_true',help='Compile the default legacy host feature configuration too')
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
    if args.without_resource_refresh:
        flags=[flag for flag in flags if not flag.startswith('-DCSOL_RESOURCE_REFRESH=')]
        flags.append('/DCSOL_RESOURCE_REFRESH=0')
    sources=[ROOT/p for p in ('tests/cryoffear-mouse-bridge/main.cpp',
        'native/csol/client/weapons/guest_cof_mouse.cpp','native/guest/cof/client/mouse_input.cpp',
        'native/csol/client/weapons/guest_cof.cpp','native/csol/client/weapons/guest_cof_ironsight.cpp',
        'native/guest/cof/aim_fov.cpp','native/guest/cof/presentation_protocol.cpp',
        'native/guest/cof/client/presentation.cpp','native/guest/cof/client/revolver_cylinder.cpp')]
    inputs=set(sources)|{Path(__file__).resolve(),args.commands.resolve(),args.environment.resolve(),
                          config,compiler,compiler.with_name('lld-link.exe')}
    env=os.environ|json.loads(args.environment.read_text('utf-8-sig'))
    temp=output/'temp';temp.mkdir();env.update(TEMP=str(temp),TMP=str(temp))
    for i,source in enumerate(sources):
        dep=output/f'{i}.d'
        argv=[str(compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),str(source)]
        result=subprocess.run(argv,cwd=ROOT,env=env,capture_output=True,text=True,timeout=90)
        (output/f'{i}-headers.log').write_text(result.stdout+result.stderr,'utf8');result.check_returncode()
        make=dep.read_text().replace(chr(92)+chr(10),' ')
        inputs.update(Path(p.replace(chr(92)+' ',' ')).resolve() for p in re.findall(r'(?:\\ |[^\s])+',make.split(':',1)[1]))
    before={str(p):sha(p) for p in sorted(inputs)}
    executable=output/'mouse-bridge.exe'
    argv=[str(compiler),*flags,'/clang:-fuse-ld=lld',*map(str,sources),'/Fe:'+str(executable),
          '/link','/machine:x86','/threads:1','/opt:ref']
    built=subprocess.run(argv,cwd=output,env=env,capture_output=True,text=True,timeout=150)
    (output/'compile.log').write_text(built.stdout+built.stderr,'utf8')
    tested=None
    if not built.returncode:
        tested=subprocess.run([str(executable)],cwd=output,capture_output=True,text=True,timeout=30)
        (output/'test.log').write_text(tested.stdout+tested.stderr,'utf8')
    after={str(p):sha(p) for p in sorted(inputs)}
    success=not built.returncode and tested is not None and not tested.returncode and before==after
    receipt=dict(success=success,command=argv,before=before,after=after,unchanged=before==after,
        compiled=built.returncode,tested=tested.returncode if tested else None,
        sourceCopies=0,resourceRefresh=not args.without_resource_refresh,gameplay=False,performance=False,
        boundary='Actual production x86 detour/lifecycle and arithmetic; controlled MetaHook installer and executable native continuation. Original instruction comparison is separate.')
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf8')
    print(tested.stdout+tested.stderr if tested else built.stdout+built.stderr,flush=True)
    raise SystemExit(0 if success else 1)


if __name__=='__main__':
    main()
