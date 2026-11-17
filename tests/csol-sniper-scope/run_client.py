#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual scope/weapon controller with current SDK inputs, in place."""
import argparse, hashlib, json, os, re, subprocess, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
CPP=['native/csol/client/'+name+'.cpp' for name in ('session','crosshair','pickup_history')]
CPP+=['native/csol/client/weapons/'+name+'.cpp' for name in
      ('weapon_inventory','weapons','beamgun','cannon','k3','melee','m249ex','balrog11','skull1','skull5','weapon_module','gun_smoke')]
CPP+=['native/csol/weapons/'+name+'.cpp' for name in ('beamgun_state','weapon_slots','melee_attacks')]
CPP+=['native/csol/client/scenario/core/death_notices.cpp','tests/csol-sniper-scope/client.cpp','tests/csol-sniper-scope/services.cpp','tests/csol-weapon-slots/session_edges.cpp']
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def ident(p):
 p=Path(p).resolve(strict=True);b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--build',type=Path,required=True);ap.add_argument('--environment',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--reuse',type=Path);ap.add_argument('--host-guard',action='store_true');args=ap.parse_args()
 build=args.build.resolve(strict=True);out=args.output.resolve()
 if not build.is_relative_to(ROOT/'build') or not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use an existing workspace cache and fresh output directory')
 sources=['native/csol/client/weapons/sniper_scope_host.cpp','tests/csol-sniper-scope/host.cpp'] if args.host_guard else CPP
 out.mkdir();steps=[];before={};success=False
 env=dict(os.environ)
 for k,v in read(args.environment).items():
  for old in list(env):
   if old.casefold()==k.casefold():del env[old]
  env[k]=v
 def run(stage,argv):
  t=time.monotonic();r=subprocess.run(argv,cwd=ROOT,env=env,capture_output=True,timeout=240);log=out/(stage+'.log');log.write_bytes(r.stdout+r.stderr)
  steps.append(dict(stage=stage,argv=argv,exit=r.returncode,seconds=time.monotonic()-t,log=ident(log)))
  print(stage,r.returncode,flush=True)
  if r.returncode:print((r.stdout+r.stderr).decode('utf8','replace')[-5000:]);r.check_returncode()
  return r.stdout
 try:
  cache=(build/'CMakeCache.txt').read_text('utf8');ninja=re.search(r'^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$',cache,re.M)[1].strip()
  raw=run('commands',[ninja,'-C',str(build),'-t','compdb']);(out/'commands.json').write_bytes(raw);database=json.loads(raw)
  command=next(x['command'] for x in database if x['file'].replace('\\','/').endswith('/native/csol/client/plugin.cpp'))
  tokens=re.findall(r'"[^"\r\n]*"|[^\s]+',command);includes=[p[2:].strip('"') for p in tokens if p.startswith('-I')];defines=[p for p in tokens if p.startswith('-D')]
  compiler=Path(read(ROOT/'.tools/clang-toolchain.json')['root'])/'bin/clang-cl.exe'
  flags=['/nologo','/TP','/utf-8','/EHsc','/MT','/std:c++20','/W3','/WX','--target=i686-pc-windows-msvc','/arch:AVX2','/O2','/clang:-O3','/clang:-flto=thin','/fp:precise','/clang:-ffp-contract=off','-Wno-overriding-option','-Wno-writable-strings',*defines,*['/imsvc'+p for p in includes]]
  dependencies={ROOT/p for p in sources};dependencies.update((ROOT/Path(__file__).relative_to(ROOT),args.environment.resolve(),ROOT/'.tools/clang-toolchain.json',build/'CMakeCache.txt',build/'build.ninja'))
  per_tu={}
  for i,rel in enumerate(sources):
   dep=out/(str(i)+'.d');run('deps-'+str(i),[str(compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),'/clang:-MT','/clang:adapter',str(ROOT/rel)])
   body=dep.read_text('utf8').replace('\\\n',' ');names=re.findall(r'(?:\\ |[^\s])+',body.split(':',1)[1]);paths={Path(n.replace('\\ ',' ')).resolve(strict=True) for n in names};dependencies.update(paths);per_tu[rel]=paths
  before={str(p):ident(p) for p in sorted(dependencies)}
  previous=read(args.reuse/'result.json') if args.reuse else None
  prior_steps=previous['steps'] if previous else []
  prior_inputs={p['path']:p for p in previous['inputs']} if previous else {}
  can_reuse=previous is not None
  objects=[]
  for i,rel in enumerate(sources):
   previous_compile=next((x for x in prior_steps if x['stage']=='compile-'+str(i) and x['exit']==0 and x['argv'][-1]==str(ROOT/rel)),None)
   stable=all(str(p) in prior_inputs and ident(p)==prior_inputs[str(p)] for p in per_tu[rel])
   same_flags=previous_compile is not None and [x for x in previous_compile['argv'] if not x.startswith('/Fo')]==[str(compiler),*flags,'/c',str(ROOT/rel)]
   if can_reuse and stable and same_flags:
    obj=Path(next(x[3:] for x in previous_compile['argv'] if x.startswith('/Fo')))
    if obj.is_file():
     objects.append(str(obj));steps.append(dict(stage='reuse-'+str(i),object=ident(obj),original=previous_compile));print('reuse-'+str(i),flush=True);continue
   obj=out/(str(i)+'.obj');run('compile-'+str(i),[str(compiler),*flags,'/c','/Fo'+str(obj),str(ROOT/rel)]);objects.append(str(obj))
  exe=out/'client.exe';run('link',[str(compiler.with_name('lld-link.exe')),'/nologo','/machine:X86','/subsystem:console','/OPT:REF','/OPT:ICF','/OPT:LLDLTO=3','/threads:1','/out:'+str(exe),*objects,'opengl32.lib','ole32.lib'])
  run('run',[str(exe)]);success=True
 finally:
  changed=[p for p,x in before.items() if ident(p)!=x];receipt=dict(success=success and not changed,steps=steps,inputs=list(before.values()),changed=changed,sourceCopies=0,gameplay=False,boundary='Actual weapon inventory, controller, module, SetFOV routing and session lifecycle; engine and unrelated UI services are controlled SDK boundaries. Rendering and live gameplay are separate.')
  (out/'result.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),'utf8')
  if changed:raise RuntimeError('Inputs changed: '+str(changed))
if __name__=='__main__':main()
