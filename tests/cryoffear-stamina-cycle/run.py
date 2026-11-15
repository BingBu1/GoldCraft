# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare production CoF stamina cycles with matched original machine code."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from oracle import generate
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sample','proof','compiler','environment','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.absolute()
    for path in (out,*out.parents):
        if path.is_symlink() or os.path.isjunction(path):raise ValueError('Linked output')
    if out.exists() or not out.resolve().is_relative_to(ROOT/'build'):raise ValueError('Fresh workspace output required')
    out.mkdir(parents=True);gitBefore=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    with (out/'cases.txt').open('w') as cases,(out/'expected.txt').open('w') as expected:
        original=generate(a.sample,a.proof,cases,expected)
    sources=[Path(__file__).with_name('main.cpp'),ROOT/'native/guest/cof/stamina_cycle.cpp']
    proof=json.loads(a.proof.read_text('utf-8-sig'))
    inputs={Path(__file__).resolve(),Path(__file__).with_name('oracle.py'),ROOT/'tests/cryoffear-player-think/oracle.py',
            a.sample.resolve(),a.proof.resolve(),a.compiler.resolve(),a.environment.resolve(),
            out/'cases.txt',out/'expected.txt',*sources}
    inputs.update(a.proof.parent/name for name in proof['archives'])
    env=dict(os.environ)
    for key,value in json.loads(a.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold()==key.casefold():del env[old]
        env[key]=value
    (out/'temp').mkdir();env.update(TEMP=str(out/'temp'),TMP=str(out/'temp'))
    flags=['/nologo','--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/utf-8','/O2','/clang:-O3',
        '/clang:-flto=thin','/clang:-fuse-ld=lld','/arch:AVX2','/fp:precise','/clang:-ffp-contract=off','/DNOMINMAX','/Gy','/Gw','/I'+str(ROOT)]
    commands=[]
    for n,source in enumerate(sources):
        dep=out/f'{n}.d';cmd=[str(a.compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),str(source)]
        run=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,timeout=60);commands.append(cmd)
        (out/f'{n}-headers.log').write_text(run.stdout+run.stderr,'utf-8')
        if run.returncode:raise RuntimeError('Dependency discovery failed')
        make=dep.read_text().replace(chr(92)+chr(10),' ')
        inputs.update(Path(v.replace(chr(92)+' ',' ')).resolve() for v in re.findall(r'(?:\\ |[^\s])+',make.split(':',1)[1]))
    before={str(v):sha(v) for v in sorted(inputs)};exe=out/'stamina.exe'
    command=[str(a.compiler),*flags,*map(str,sources),'/Fe:'+str(exe),'/link','/machine:x86','/threads:1','/opt:ref']
    compiled=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True,timeout=120)
    (out/'compile.log').write_text(compiled.stdout+compiled.stderr,'utf-8');tested=None;negative=[]
    if not compiled.returncode:
        tested=subprocess.run([str(exe),str(out/'cases.txt'),str(out/'expected.txt')],cwd=out,capture_output=True,text=True,timeout=60)
        (out/'test.log').write_text(tested.stdout+tested.stderr,'utf-8')
    if tested is not None and not tested.returncode:
        text=(out/'expected.txt').read_text();lines=text.splitlines(keepends=True);wrong=lines.copy()
        row=wrong[0].split();row[0]=format(int(row[0],16)^1,'x');wrong[0]=' '.join(row)+'\n'
        for name,value in (('empty',''),('truncated',''.join(lines[:-1])),('extra',text+'0\n'),('wrong',''.join(wrong))):
            path=out/('bad-'+name+'.txt');path.write_text(value)
            run=subprocess.run([str(exe),str(out/'cases.txt'),str(path)],cwd=out,capture_output=True,text=True,timeout=60)
            (out/('bad-'+name+'.log')).write_text(run.stdout+run.stderr,'utf-8')
            negative.append(dict(name=name,exit=run.returncode,passed=run.returncode==2))
    after={str(v):sha(v) for v in sorted(inputs)}
    success=not compiled.returncode and tested is not None and not tested.returncode and before==after and all(v['passed'] for v in negative)
    (out/'receipt.json').write_text(json.dumps(dict(success=success,gitBefore=gitBefore,
        gitAfter=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        original=original,command=command,dependencyCommands=commands,dependencyBefore=before,dependencyAfter=after,
        compiled=compiled.returncode,tested=tested.returncode if tested else None,negative=negative,
        sourceCopies=0,deployed=False,gameplay=False,performance=False),indent=2),'utf-8')
    print(tested.stdout+tested.stderr if tested else compiled.stdout+compiled.stderr)
    raise SystemExit(0 if success else 1)
if __name__=='__main__':main()
