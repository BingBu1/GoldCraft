#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the real resource loader/collector with an explicit frozen SDK baseline."""
import argparse,hashlib,json,os,re,shutil,subprocess,time
from pathlib import Path
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def key(p):return os.path.normcase(str(Path(p).resolve()))
def ident(p):
 p=Path(p).resolve();return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def verify(row):
 now=ident(row['path']);assert now==row,row['path'];return now
def save(p,v):p.write_text(json.dumps(v,indent=2),encoding='utf8')
p=argparse.ArgumentParser(description=__doc__)
for n in ('baseline','candidate','loader','loader-headers','common','output'):p.add_argument('--'+n,type=Path,required=True)
p.add_argument('--negative',action='store_true');p.add_argument('--reuse-collector',type=Path)
a=p.parse_args();root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir();source=out/'source';output=out/'output';output.mkdir()
if a.reuse_collector:a.reuse_collector=a.reuse_collector.resolve()
baseline=read(a.baseline/'plan.json');previous=read(a.baseline/'output/result.json');assert previous['success'] and previous['inputsUnchanged']
inputs={};copies={}
def keep(path):inputs[key(path)]=ident(path);return inputs[key(path)]
def copy(rel,src,expected=None):
 src=Path(src);assert expected is None or ident(src)['sha256']==expected
 dest=source/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dest);assert dest.stat().st_nlink==1
 copies[rel]=dict(original=ident(src),frozen=ident(dest));return dest
for r in baseline['inputs']:verify(r['frozen']);copy(r['relative'],r['frozen']['path'],r['frozen']['sha256'])
for rel in ['tests/csol-dependency-cache/loader.cpp','tests/csol-dependency-cache/metadata.inc']:
 copy(rel,root/rel)
for r in read(a.loader_headers):
 if r['path'] in ['native/engine/csol_dependency_cache.hpp','native/engine/csol_loader.h','native/include/goldcraft/resource_stamp.hpp']:
  copy(r['path'],a.loader_headers.parent/'after'/r['path'],r['sha256'])
candidate={r['path']:r for r in read(a.candidate)['files']}
selected=set()
def closure(rel):
 if rel in selected:return
 selected.add(rel)
 if rel in candidate:r=candidate[rel];src=Path(r['source']);expected=r['sha256']
 elif rel in copies:src=Path(copies[rel]['original']['path']);expected=copies[rel]['original']['sha256']
 else:src=root/rel;expected=None
 dest=copy(rel,src,expected)
 for token in re.findall(r'^\s*#include\s+"([^"]+)"',dest.read_text('utf8'),re.M):
  child=Path(os.path.normpath(str(Path(rel).parent/token))).as_posix()
  if child.startswith('native/') and ((root/child).is_file() or child in candidate):closure(child)
closure('native/csol/dependencies.cpp');closure('native/csol/weapon_resources.hpp')
loader=copy('native/engine/csol_loader.cpp',a.loader)
if a.negative:
 text=loader.read_text('utf8');before='Q_strnicmp(textures[i].name,"cso_",4)';assert text.count(before)==1
 loader.write_text(text.replace(before,'Q_strncmp(textures[i].name,"cso_",4)'),encoding='utf8');copies['native/engine/csol_loader.cpp']['frozen']=ident(loader)
copy('tests/csol-model-resource-gate/main.cpp',root/'tests/csol-model-resource-gate/main.cpp')
for r in copies.values():keep(r['frozen']['path'])
for row in baseline['system']+baseline['tools']+baseline['libraries']+[baseline['environment']]:inputs[key(row['path'])]=verify(row)
for path in [__file__,a.candidate,a.loader,a.loader_headers,a.baseline/'plan.json',a.baseline/'output/result.json']:keep(path)
env=os.environ.copy();env.update(read(baseline['environment']['path']));env.update(TEMP=str(output),TMP=str(output),VSLANG='1033')
original=next(x['argv'] for x in baseline['commands'] if x['name']=='loader');flags=[]
for arg in original[1:]:
 if arg.endswith('.cpp') or arg.startswith('/Fo'):continue
 flags.append(arg.replace(str(a.baseline.resolve()/'source'),str(source)))
units=['native/csol/'+name+'.cpp' for name in ('dependencies','model_codec','sprite_codec','texture_model','model_json','events','model_source','sequences')]
units.append('tests/csol-model-resource-gate/main.cpp');commands=[];objects=[];reused=[]
for rel in units:
 name=Path(rel).stem;obj=output/(name+'.obj')
 if a.reuse_collector and name!='main':
  old=read(a.reuse_collector/'result.json');assert old['inputsUnchanged']
  step=next(s for s in old['steps'] if s['name']==name);assert step['exit']==0
  for row in step['includes']:
   q=Path(row['path']);verify(row)
   if q.is_relative_to(a.reuse_collector.resolve()/'source'):assert ident(source/q.relative_to(a.reuse_collector.resolve()/'source'))['sha256']==row['sha256']
  assert ident(source/rel)['sha256']==ident(a.reuse_collector/'source'/rel)['sha256']
  obj=a.reuse_collector/'output'/(name+'.obj');oldrow=next(x for x in old['objects'] if key(x['path'])==key(obj));verify(oldrow);keep(obj);reused.append(oldrow)
 else:commands.append(dict(name=name,kind='compile',argv=[original[0],*flags,str(source/rel),'/Fo'+str(obj)],seconds=180))
 objects.append(str(obj))
exe=output/'resource_gate.exe'
commands.append(dict(name='link',kind='link',argv=[baseline['tools'][1]['path'],'/nologo','/machine:x86','/debug','/opt:ref','/opt:icf','/out:'+str(exe),'/pdb:'+str(output/'resource_gate.pdb'),*objects,str(root/'external/ReHLDS/rehlds/lib/libacof32.lib')],seconds=180))
commands.append(dict(name='case',kind='test',argv=[str(exe),str(a.common.resolve())],seconds=120))
save(out/'plan.json',dict(commands=commands,inputs=list(inputs.values()),copies=copies,negative=a.negative,reused=reused))
result=dict(success=False,steps=[],unknown=[],negative=a.negative,objects=[],boundary='Actual complete loader/collector and SDK; FS/console boundaries are controlled. No full engine deployment.')
try:
 for c in commands:
  start=time.monotonic();log=output/(c['name']+'.log')
  run=subprocess.run(c['argv'],cwd=output,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=c['seconds'],creationflags=subprocess.CREATE_NO_WINDOW);log.write_bytes(run.stdout)
  text=run.stdout.decode('utf8',errors='replace');included=[]
  for value in set(re.findall(r'Note: including file:\s*(.+)',text)):
   row=ident(value.strip());included.append(row)
   if key(row['path']) not in inputs:result['unknown'].append(row)
   else:assert row==inputs[key(row['path'])],row['path']
  expected=1 if a.negative and c['kind']=='test' else 0
  result['steps'].append(dict(name=c['name'],exit=run.returncode,expectedExit=expected,seconds=time.monotonic()-start,log=ident(log),includes=included))
  print(c['name'],run.returncode,flush=True);assert not result['unknown'] and run.returncode==expected,c['name']
  if c['kind']=='compile':result['objects'].append(ident(output/(c['name']+'.obj')))
  if c['kind']=='test' and a.negative:assert 'resourceBatch && resourceBatch->models==1' in text
 result['success']=True;result['executable']=ident(exe)
except BaseException as error:result['error']=repr(error)
result['inputDrift']=[r['path'] for r in inputs.values() if ident(r['path'])!=r];result['inputsUnchanged']=not result['inputDrift'];result['success'] &= result['inputsUnchanged']
save(out/'result.json',result);print(json.dumps({k:result.get(k) for k in ['success','error','inputsUnchanged','negative']}))
raise SystemExit(0 if result['success'] else 1)
