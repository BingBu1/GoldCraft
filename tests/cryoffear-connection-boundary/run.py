#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build real connection cleanup entries and real Win32 API lookup checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('native/client/precache_client.cpp', 'tests/cryoffear-connection-boundary/main.cpp',
           'tests/cryoffear-connection-boundary/lookup_provider.cpp',
           'tests/cryoffear-connection-boundary/lookup.cpp')

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--commands', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--resource-owner-wip', action='store_true',
                        help='Explicitly link current unpublished resource-owner dependencies; never needed by published baseline')
    args = parser.parse_args()
    extra_sources = ['native/client/resource_model_cache.cpp'] if args.resource_owner_wip else []
    out = args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():
        raise ValueError('Use a new dedicated build directory')
    out.mkdir(parents=True)
    (out/'temp').mkdir()
    env = os.environ.copy()
    env.update({k:v for k,v in json.loads(args.environment.read_text('utf-8-sig')).items()
                if k in ('INCLUDE','LIB','LIBPATH','PATH')})
    env['TEMP'] = env['TMP'] = str(out/'temp')
    config = json.loads((ROOT/'.tools/clang-toolchain.json').read_text('utf-8-sig'))
    compiler = Path(config['root'])/'bin/clang-cl.exe'
    linker = compiler.with_name('lld-link.exe')
    database = json.loads(args.commands.read_text('utf-8'))
    command = next(x['command'] for x in database
                   if x['file'].replace(chr(92),'/').endswith('/native/client/precache_client.cpp'))
    tokens = re.findall(r'"[^"\r\n]*"|[^\s]+', command)
    includes = [x for x in tokens if x.startswith('-I')]
    defines = [x for x in tokens if x.startswith('-D') and not x.startswith('-DGOLDCRAFT_RESOURCE_SYNC=')]
    flags = ['/nologo','/c','/TP','/std:c++20','/EHsc','/MT','/utf-8','/W3',
             '--target=i686-pc-windows-msvc','/O2','/clang:-O3','/clang:-flto=thin','/arch:AVX2',
             '/fp:precise','/Gy','/Gw','/DNDEBUG','/wd4311','/wd4312','/wd4996',*defines,*includes]
    tracked = [ROOT/s for s in (*SOURCES,*extra_sources,'native/client/precache_client.hpp',
               'native/client/GoldCraft.def','native/client/precache_10210.json',
               'native/include/goldcraft/connection_api.h','tests/cryoffear-connection-boundary/provider.def',
               'tests/cryoffear-connection-boundary/run.py')]
    tracked += [compiler,linker,args.commands.resolve(),args.environment.resolve()]
    before = {str(p):sha(p) for p in tracked}
    dependencies, steps = {}, []

    def execute(name, argv):
        p = subprocess.run(list(map(str,argv)),cwd=ROOT,env=env,capture_output=True,text=True)
        (out/(name+'.log')).write_text(p.stdout+p.stderr,'utf-8')
        steps.append(dict(name=name,command=list(map(str,argv)),exit=p.returncode))
        (out/'steps.json').write_text(json.dumps(steps,indent=2),'utf-8')
        print(name,p.returncode,flush=True)
        if p.returncode:
            print(p.stdout+p.stderr,flush=True)
            p.check_returncode()
        return p.stdout+p.stderr

    # Discover and freeze the compiler's true SDK/local include closure before
    # generating code. No source tree is copied and no fake SDK header is used.
    for i,source in enumerate((*SOURCES,*extra_sources)):
        dep = out/(str(i)+'.d')
        execute('dependencies-'+str(i),[compiler,*[f for f in flags if f!='/c'],
                '/clang:-M','/clang:-MF','/clang:'+str(dep),'/clang:-MT','/clang:connection',ROOT/source])
        text = dep.read_text('utf-8').replace('\\\n',' ')
        for name in re.findall(r'(?:\\ |[^\s])+',text.split(':',1)[1]):
            p = Path(name.replace('\\ ',' ')).resolve()
            dependencies[str(p)] = sha(p)
    extra_objects = []
    for source in extra_sources:
        obj = out/(Path(source).stem+'.obj')
        execute('compile-'+Path(source).stem,[compiler,*flags,'/Fo'+str(obj),ROOT/source])
        extra_objects.append(obj)
    for mode in (0,1):
        settings = ['/DGOLDCRAFT_RESOURCE_SYNC='+str(mode),'/DCSOL_RESOURCE_REFRESH='+str(mode)]
        actual = out/f'actual-sdk-{mode}.obj'
        execute(f'actual-sdk-{mode}',[compiler,*flags,*settings,'/Fo'+str(actual),ROOT/SOURCES[0]])
        obj = out/f'connection-{mode}.obj'
        execute(f'fixture-{mode}',[compiler,*flags,*settings,'/Fo'+str(obj),ROOT/SOURCES[1]])
        exe = out/f'connection-{mode}.exe'
        execute(f'link-{mode}',[linker,'/nologo','/machine:X86','/subsystem:console','/OPT:REF','/OPT:ICF',
                              '/out:'+str(exe),obj,*extra_objects,'opengl32.lib','bcrypt.lib'])
        execute(f'test-{mode}',[exe])
    for name,source in [('provider',SOURCES[2]),('lookup',SOURCES[3])]:
        execute('compile-'+name,[compiler,*flags,'/Fo'+str(out/(name+'.obj')),ROOT/source])
    dll = out/'GoldCraft.dll'
    execute('link-provider',[linker,'/nologo','/dll','/machine:X86','/OPT:REF','/OPT:ICF',
                            '/def:'+str(ROOT/'tests/cryoffear-connection-boundary/provider.def'),
                            '/out:'+str(dll),out/'provider.obj'])
    exe = out/'lookup.exe'
    execute('link-lookup',[linker,'/nologo','/machine:X86','/subsystem:console','/OPT:REF','/OPT:ICF',
                          '/out:'+str(exe),out/'lookup.obj'])
    execute('test-lookup',[exe,dll])
    after = {p:sha(p) for p in before}
    changed = [p for p,h in dependencies.items() if sha(p)!=h]
    if before!=after or changed: raise RuntimeError('Build inputs changed: '+repr(changed))
    receipt = dict(revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                   before=before,after=after,dependencies=dependencies,flags=flags,steps=steps,
                   inputClosureStable=True,sourceCopies=False,productCleanupEntries=True,
                   resourceOwnerSnapshot=True,resourceOwnerWip=args.resource_owner_wip,
                   actualWindowsLookup=True,fullDll=False,liveDisconnect=False,
                   performanceAccepted=False,results={p.name:p.read_text('utf-8') for p in out.glob('test-*.log')},
                   boundary='Actual x86 SDK production TU compile plus real clear/disconnect/reset with controlled engine trampolines and resource-owner overlay snapshot state; test provider DLL checks real Win32 lookup only, not GoldCraft full DLL')
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf-8')

if __name__ == '__main__': main()
