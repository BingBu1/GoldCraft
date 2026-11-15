#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise exact map-vote shutdown bodies with the real matched VGUI callback ABI."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('environment','commands','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();out=args.output.absolute()
    if out.exists() or not out.resolve().is_relative_to(ROOT/'build') or any(p.is_symlink() or os.path.isjunction(p) for p in (out,*out.parents)):raise ValueError('Use a fresh workspace build directory')
    out.mkdir(parents=True)
    helper=ROOT/'tests/common/cpp_function.py';spec=importlib.util.spec_from_file_location('extract',helper);extract=importlib.util.module_from_spec(spec);spec.loader.exec_module(extract)
    source=ROOT/'native/csol/client/scenario/core/scenario_map_vote.cpp';raw=source.read_bytes()
    _,_,body=extract.function(raw,b'class ClientLifetime final:');(out/'ui-lifetime.inc').write_bytes(body+b'clientLifetime;\n')
    _,_,body=extract.function(raw,b'void ShutdownScenarioMapVote()');(out/'ui-public-shutdown.inc').write_bytes(body+b'\n')
    toolchain=ROOT/'.tools/clang-toolchain.json';compiler=Path(json.loads(toolchain.read_text('utf-8-sig'))['root'])/'bin/clang-cl.exe'
    db=json.loads(args.commands.read_text());command=next(x['command'] for x in db if x['file'].replace(chr(92),'/').endswith('/client/scenario/core/scenario_map_vote.cpp'));tokens=re.findall(r'"[^"\r\n]*"|[^\s]+',command)
    flags=['/nologo','/TP','/std:c++20','/EHsc','/MT','/utf-8','/W3','/WX','--target=i686-pc-windows-msvc','/O2','/clang:-O3','/clang:-flto=thin','/arch:AVX2','/fp:precise','/clang:-ffp-contract=off','-Wno-overriding-option',*[s for s in tokens if s.startswith('-D')],*['/imsvc'+s[2:] for s in tokens if s.startswith('-I')],'/I'+str(out)]
    env=os.environ|json.loads(args.environment.read_text('utf-8-sig'));temp=out/'temp';temp.mkdir();env.update(TEMP=str(temp),TMP=str(temp))
    test=ROOT/'tests/cryoffear-scene-admission/ui_shutdown.cpp';dep=out/'headers.d'
    headers=subprocess.run([str(compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),str(test)],env=env,cwd=out,capture_output=True,text=True);(out/'headers.log').write_text(headers.stdout+headers.stderr);headers.check_returncode()
    make=dep.read_text().replace(chr(92)+chr(10),' ');inputs={source,test,helper,Path(__file__).resolve(),compiler,compiler.with_name('lld-link.exe'),toolchain,args.commands.resolve(),args.environment.resolve()}
    inputs.update(Path(p.replace(chr(92)+' ',' ')).resolve() for p in re.findall(r'(?:\\ |[^\s])+',make.split(':',1)[1]));before={str(p):sha(p) for p in sorted(inputs)}
    exe=out/'ui-shutdown.exe';built=subprocess.run([str(compiler),*flags,'/clang:-fuse-ld=lld',str(test),'/Fe:'+str(exe),'/link','/machine:x86','/threads:1','/opt:ref'],env=env,cwd=out,capture_output=True,text=True,timeout=90);(out/'compile.log').write_text(built.stdout+built.stderr)
    tested=None
    if built.returncode==0:
        tested=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10);(out/'test.log').write_text(tested.stdout+tested.stderr)
    after={str(p):sha(p) for p in sorted(inputs)};success=built.returncode==0 and tested is not None and tested.returncode==0 and before==after
    (out/'receipt.json').write_text(json.dumps(dict(success=success,unchanged=before==after,before=before,after=after,compiled=built.returncode,tested=tested.returncode if tested else None,sourceCopies=0,gameplay=False),indent=2),'utf8')
    print(tested.stdout+tested.stderr if tested else built.stdout+built.stderr);raise SystemExit(0 if success else 1)
if __name__=='__main__':main()
