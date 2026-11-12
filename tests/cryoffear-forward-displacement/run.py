# SPDX-License-Identifier: GPL-3.0-or-later
"""Build actual scalar consumer and compare SHA-pinned original instructions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from oracle import verify

ROOT=Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('environment','sample','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();output=args.output.absolute()
    if output.exists() or not output.resolve().is_relative_to(ROOT/'build') or any(
            p.is_symlink() or os.path.isjunction(p) for p in (output,*output.parents)):
        raise ValueError('Use a fresh workspace build directory')
    output.mkdir(parents=True)
    config=ROOT/'.tools/clang-toolchain.json'
    compiler=Path(json.loads(config.read_text('utf-8-sig'))['root'])/'bin/clang-cl.exe'
    flags=['/nologo','--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/utf-8',
           '/O2','/clang:-O3','/clang:-flto=thin','/clang:-fuse-ld=lld','/arch:AVX2',
           '/fp:precise','/clang:-ffp-contract=off','/Gy','/Gw']
    sources=[ROOT/'tests/cryoffear-forward-displacement/main.cpp',ROOT/'native/guest/cof/client/forward_displacement.cpp']
    inputs=set(sources)|{Path(__file__).resolve(),Path(__file__).with_name('oracle.py'),
        ROOT/'tests/cryoffear-aim/oracle.py',args.sample.resolve(),args.environment.resolve(),config,
        compiler,compiler.with_name('lld-link.exe')}
    env=os.environ|json.loads(args.environment.read_text('utf-8-sig'))
    for i,source in enumerate(sources):
        dep=output/f'{i}.d'
        command=[str(compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),str(source)]
        run=subprocess.run(command,cwd=output,env=env,capture_output=True,text=True,timeout=60)
        (output/f'{i}-headers.log').write_text(run.stdout+run.stderr,'utf-8');run.check_returncode()
        make=dep.read_text().replace(chr(92)+chr(10),' ')
        inputs.update(Path(p.replace(chr(92)+' ',' ')).resolve() for p in re.findall(r'(?:\\ |[^\s])+',make.split(':',1)[1]))
    before={str(p):sha(p) for p in sorted(inputs)}
    executable=output/'forward.exe'
    command=[str(compiler),*flags,*map(str,sources),'/Fe:'+str(executable),'/link','/machine:x86','/threads:1','/opt:ref']
    compiled=subprocess.run(command,cwd=output,env=env,capture_output=True,text=True,timeout=120)
    (output/'compile.log').write_text(compiled.stdout+compiled.stderr,'utf-8');compiled.check_returncode()
    original=verify(args.sample.resolve(),executable,output/'differential.json')
    after={str(p):sha(p) for p in sorted(inputs)}
    receipt=dict(success=before==after,command=command,before=before,after=after,unchanged=before==after,
        original=original,sourceCopies=0,gameplay=False,performance=False)
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf-8')
    print(json.dumps(dict(success=receipt['success'],cases=original['cases'],byteMismatches=0)),flush=True)
    raise SystemExit(0 if receipt['success'] else 1)


if __name__=='__main__':
    main()
