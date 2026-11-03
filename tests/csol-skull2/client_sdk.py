#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile frozen complete client TUs and exercise their actual SDK boundary."""
import argparse,hashlib,json,os,re,shutil,subprocess,time
from pathlib import Path

def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def key(p):return os.path.normcase(str(Path(p).resolve()))
def identity(p):
 p=Path(p).resolve();return dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p))
def write(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--manifest',type=Path,required=True);ap.add_argument('--overlay',type=Path,required=True)
 ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
 ap.add_argument('--smoke-overlay',type=Path,help='Test shared SKULL1/2/5 smoke in six fresh process orders')
 a=ap.parse_args();root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir();source=out/'source'
 selected={r['path']:(r,a.manifest.parent/'source'/r['path']) for r in read(a.manifest)}
 selected.update({r['path']:(r,a.overlay.parent/'source'/r['path']) for r in read(a.overlay)})
 if a.smoke_overlay:selected.update({r['path']:(r,a.smoke_overlay.parent/'source'/r['path']) for r in read(a.smoke_overlay)})
 excluded=[dict(relative=name,expected=row['sha256'],current=identity(root/name),reason='Not a client compilation input') for name,(row,_) in selected.items() if not name.startswith('native/csol/') or '/server/' in name or '/config/' in name]
 selected={name:value for name,value in selected.items() if name.startswith('native/csol/') and '/server/' not in name and '/config/' not in name}
 inputs={};production=[]
 for name,(row,original) in selected.items():
  assert identity(original)['sha256']==row['sha256'] and original.stat().st_size==row['bytes']
  current=root/name;assert sha(current)==row['sha256'];production.append(identity(current))
  dest=source/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(original,dest);assert dest.stat().st_nlink==1
  inputs[key(original)]=identity(original);inputs[key(dest)]=identity(dest)
 for tree in ('native/csol','native/include'):
  for original in (root/tree).rglob('*'):
   if original.is_file() and original.suffix in ('.h','.hpp','.inl','.inc'):
    dest=source/original.relative_to(root)
    if not dest.exists():dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(original,dest)
    inputs[key(dest)]=identity(dest)
 for relative in ('native/csol/client/shared_random.cpp','native/csol/client/weapon_module.cpp','tests/csol-skull2/client.cpp'):
  original=root/relative;dest=source/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(original,dest)
  inputs[key(original)]=identity(original);inputs[key(dest)]=identity(dest)
 if a.smoke_overlay:
  original=root/'tests/csol-skull2/smoke.cpp';dest=source/'tests/csol-skull2/smoke.cpp';shutil.copy2(original,dest)
  inputs[key(original)]=identity(original);inputs[key(dest)]=identity(dest);inputs[key(a.smoke_overlay)]=identity(a.smoke_overlay)
 baseline=read(a.baseline);env=os.environ.copy();env.update(baseline['environment']);(out/'temp').mkdir()
 env.update(TEMP=str(out/'temp'),TMP=str(out/'temp'),VSLANG='1033')
 for row in baseline['inputs']:
  p=Path(row['path'])
  if p.is_file() and '/source/' not in p.as_posix():inputs[key(p)]=identity(p)
 for p in (a.manifest,a.overlay,a.baseline,Path(__file__)):inputs[key(p)]=identity(p)
 original=baseline['commands'][0]['argv'];common=[]
 for arg in original:
  if arg=='--':break
  if arg.startswith(('/Fo','/Fd')) or arg=='-c':continue
  if '/sdk-01/generated' in arg.replace('\\','/'):continue
  common.append(arg)
 commands=[]
 units=[('skull1','native/csol/client/skull1.cpp'),('skull5','native/csol/client/skull5.cpp'),('fixture','tests/csol-skull2/smoke.cpp')] if a.smoke_overlay else [('skull2','native/csol/client/skull2.cpp'),('skull5','native/csol/client/skull5.cpp'),('fixture','tests/csol-skull2/client.cpp')]
 for name,relative in units:
  commands.append(dict(name=name,argv=common+['/c',str(source/relative),'/Fo'+str(out/(name+'.obj')),'/Fd'+str(out/(name+'.pdb'))]))
 extra=[str(out/'skull1.obj')] if a.smoke_overlay else []
 commands.append(dict(name='link',argv=[original[0],'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',str(out/'fixture.obj'),str(out/'skull5.obj'),*extra,'/Fe'+str(out/'client.exe'),'/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1']))
 if a.smoke_overlay:commands.extend(dict(name='order-'+str(i),argv=[str(out/'client.exe'),str(i)]) for i in range(6))
 else:commands.append(dict(name='test',argv=[str(out/'client.exe')]))
 write(out/'plan.json',dict(commands=commands,inputs=list(inputs.values()),production=production,excludedManifestPaths=excluded,jobs=1,boundary='Actual frozen complete TUs, declared engine boundary; no full DLL/game/network.'))
 result=dict(success=False,steps=[],unknown=[],changedInputs=[],changedProduction=[]);start=time.monotonic()
 try:
  for command in commands:
   log=out/(command['name']+'.log');then=time.monotonic()
   with log.open('xb') as stream:p=subprocess.run(command['argv'],env=env,cwd=out,stdout=stream,stderr=subprocess.STDOUT,timeout=180)
   text=log.read_text('utf8',errors='replace');actual=[]
   for path in re.findall(r'^\s*Note: including file:\s*(.*?)\s*$',text,re.M):
    if key(path) not in inputs:result['unknown'].append(path)
    else:actual.append(inputs[key(path)])
   result['steps'].append(dict(name=command['name'],exit=p.returncode,seconds=time.monotonic()-then,log=identity(log),includes=actual))
   print(command['name'],p.returncode,flush=True)
   if p.returncode or result['unknown']:break
  result['success']=len(result['steps'])==len(commands) and all(x['exit']==0 for x in result['steps']) and not result['unknown']
 except BaseException as e:result['error']=repr(e)
 finally:
  for row in inputs.values():
   if identity(row['path'])!=row:result['changedInputs'].append(row['path'])
  for row in production:
   if identity(row['path'])!=row:result['changedProduction'].append(row['path'])
  if result['changedInputs'] or result['changedProduction']:result['success']=False
  result['seconds']=time.monotonic()-start;result['artifacts']=[identity(p) for p in out.glob('*.obj')]+[identity(p) for p in out.glob('*.exe')]
  write(out/'result.json',result);print(json.dumps({k:v for k,v in result.items() if k not in ('steps','artifacts')}))
 return 0 if result['success'] else 1
if __name__=='__main__':raise SystemExit(main())
