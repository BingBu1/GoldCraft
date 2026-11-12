#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile and execute the x86 ironsight detour with the matching real SDK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('native/csol/client/weapons/guest_cof_ironsight.cpp',
           'tests/cryoffear-ironsight/main.cpp')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--environment', type=Path, required=True)
    p.add_argument('--commands', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():
        p.error('Use a new dedicated build directory')
    out.mkdir(parents=True)
    (out/'temp').mkdir()
    env = os.environ.copy()
    env.update({k:v for k,v in json.loads(args.environment.read_text('utf-8-sig')).items()
                if k in ('INCLUDE','LIB','LIBPATH','PATH')})
    env['TEMP'] = env['TMP'] = str(out/'temp')
    config = json.loads((ROOT/'.tools/clang-toolchain.json').read_text('utf-8-sig'))
    compiler = Path(config['root'])/'bin/clang-cl.exe'
    linker = compiler.with_name('lld-link.exe')
    db = json.loads(args.commands.read_text('utf-8'))
    cmd = next(e['command'] for e in db
               if e['file'].replace(chr(92),'/').endswith('/native/csol/client/plugin.cpp'))
    tokens = re.findall(r'"[^"\r\n]*"|[^\s]+', cmd)
    flags = ['/nologo','/c','/TP','/std:c++20','/EHsc','/MT','/utf-8','/W3','/WX',
             '--target=i686-pc-windows-msvc','/O2','/clang:-O3','/clang:-flto=thin',
             '/arch:AVX2','/fp:precise','/clang:-ffp-contract=off','-Wno-overriding-option',
             '/DNDEBUG',*[s for s in tokens if s.startswith('-D')],
             *['/imsvc'+s[2:] for s in tokens if s.startswith('-I')]]
    inputs = [ROOT/s for s in (*SOURCES, 'native/csol/client/weapons/guest_cof_ironsight.hpp',
              'tests/cryoffear-ironsight/run.py')]
    inputs += [compiler, linker, args.commands.resolve(), args.environment.resolve()]
    before = {str(s):sha(s) for s in inputs}
    dependencies, steps = {}, []

    def execute(name, argv):
        r = subprocess.run(list(map(str,argv)),cwd=ROOT,env=env,capture_output=True,text=True)
        (out/(name+'.log')).write_text(r.stdout+r.stderr,'utf-8')
        steps.append(dict(name=name,argv=list(map(str,argv)),exit=r.returncode))
        (out/'steps.json').write_text(json.dumps(steps,indent=2),'utf-8')
        if r.returncode:
            print(r.stdout+r.stderr,flush=True)
            r.check_returncode()
        return r.stdout+r.stderr

    for i,s in enumerate(SOURCES):
        dep = out/(str(i)+'.d')
        execute('dependencies-'+str(i),[compiler,*[f for f in flags if f!='/c'],
                '/clang:-M','/clang:-MF','/clang:'+str(dep),'/clang:-MT','/clang:particles',ROOT/s])
        text = dep.read_text('utf-8').replace('\\\n',' ')
        for name in re.findall(r'(?:\\ |[^\s])+',text.split(':',1)[1]):
            path = Path(name.replace('\\ ',' ')).resolve()
            dependencies[str(path)] = sha(path)
        execute('compile-'+str(i),[compiler,*flags,'/Fo'+str(out/(Path(s).stem+'.obj')),ROOT/s])
    exe = out/'ironsight.exe'
    execute('link',[linker,'/nologo','/machine:X86','/subsystem:console','/OPT:REF','/OPT:ICF',
            '/out:'+str(exe),*[out/(Path(s).stem+'.obj') for s in SOURCES]])
    result = execute('test',[exe])
    after = {s:sha(s) for s in before}
    changed = [s for s,h in dependencies.items() if sha(s)!=h]
    if before!=after or changed:
        raise RuntimeError('Inputs changed during test: '+repr(changed))
    receipt = dict(revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                   before=before,after=after,dependencies=dependencies,steps=steps,flags=flags,
                   result=result,executableSha256=sha(exe),sourceCopies=False,
                   actualSdk=True,fullDll=False,live=False,performance=False)
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf-8')
    print(result.strip())


if __name__ == '__main__':
    main()
