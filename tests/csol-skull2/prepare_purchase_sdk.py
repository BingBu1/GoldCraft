#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Link transaction tests with a verified complete production SDK object set."""
import argparse,hashlib,json,os,shutil
from pathlib import Path

def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def identity(p):
 p=Path(p).resolve();return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def key(p):return os.path.normcase(str(Path(p).resolve()))
def write(p,v):p.write_text(json.dumps(v,indent=2),encoding='utf8')
p=argparse.ArgumentParser(description=__doc__)
for name in ('baseline','template','edges','output'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--override',type=Path)
a=p.parse_args();root=Path(__file__).resolve().parents[2];base=a.baseline.resolve();out=a.output.resolve();out.mkdir()
assert read(base/'build-result.json')['success'] and read(base/'dependency-audit.json')['success']
units=read(base/'complete-object-provenance.json')['units']
for u in units:assert identity(u['object']['path'])['sha256']==u['object']['sha256']
inputs={key(x['path']):identity(x['path']) for x in read(base/'inputs.json')['files']}
overlay={key(x['name']):x for x in read(base/'source-overlay.json')['roots']}
selected={x['relative']:Path(x['frozen']['path']) for x in read(base/'effective-source-manifest.json')['files']}
overrides={}
if a.override:
 for row in read(a.override):
  rel=row['path'];src=a.override.resolve().parent/'source'/rel
  assert identity(src)['sha256']==row['sha256']
  dst=out/'source'/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
  selected[rel]=dst;overrides[rel]=dst;inputs[key(dst)]=identity(dst)
 for mapping in overlay.values():
  for rel,src in overrides.items():
   if mapping['name'].replace('\\','/').lower().endswith('/'+rel.lower()):mapping['external-contents']=str(src)
fixtures=['tests/csol-skull2/purchase_sdk.cpp','tests/csol-skull2/lifecycle_sdk.cpp',
 'tests/csol-scenario-rules-session/sdk_engine.hpp','tests/csol-scenario-rules-session/resources.hpp',
 'tests/csol-scenario-map-session/fixtures.hpp','native/csol/config/skull1_damage.json','native/csol/config/skull2_damage.json']
for rel in fixtures:
 src=root/rel;dst=out/'source'/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
 selected[rel]=dst;inputs[key(dst)]=identity(dst)
for name in ('engine_edge_names.inc','engine_edge_defaults.inc'):
 rel='tests/csol-scenario-rules-session/'+name;dst=out/'source'/rel;shutil.copy2(a.edges/name,dst)
 selected[rel]=dst;inputs[key(dst)]=identity(dst)
for rel,src in selected.items():
 for alias in (root/rel,out/'source'/rel,src):
  overlay[key(alias)]=dict(type='file',name=str(alias),**{'external-contents':str(src)})
write(out/'overlay.json',dict(version=0,**{'case-sensitive':False,'use-external-names':True},roots=list(overlay.values())))
template=read(a.template/'prepared.json')
for row in template['inputs']:
 # The complete DLL can omit test-only STL headers such as iostream. Reuse
 # their already verified toolchain identities, never arbitrary live source.
 path=Path(row['path']).resolve()
 if not path.is_relative_to(root):
  now=identity(path);assert now['sha256']==row['sha256'];inputs[key(path)]=now
command=list(next(j['command'] for j in template['jobs'] if j['name']=='lifecycle_sdk'))
for i,arg in enumerate(command):
 if arg.endswith('.cpp'):command[i]=str(selected['tests/csol-skull2/purchase_sdk.cpp'])
 elif arg.startswith('/Fo'):command[i]='/Fo'+str(out/'purchase_sdk.obj')
 elif arg.startswith('/clang:') and arg.endswith('overlay.json'):command[i]='/clang:'+str(out/'overlay.json')
link=[]
replacement_objects={};jobs=[]
for rel,src in overrides.items():
 if not rel.endswith('.cpp'):continue
 matching=[u for u in units if u['source']['path'].replace('\\','/').lower().endswith('/'+rel.lower())]
 assert len(matching)==1,rel
 target=out/(Path(rel).stem+'.obj');cmd=list(command)
 for i,arg in enumerate(cmd):
  if arg.endswith('.cpp'):cmd[i]=str(src)
  elif arg.startswith('/Fo'):cmd[i]='/Fo'+str(target)
 jobs.append(dict(name=Path(rel).stem,command=cmd));replacement_objects[key(matching[0]['object']['path'])]=str(target)
for arg in read(base/'build-commands.json')[-1]['command']:
 if arg.lower()=='/dll' or arg.lower().startswith(('/out:','/pdb:','/implib:','/def:','/ltcgout:','/manifest','/natvis:')):continue
 if key(arg) in replacement_objects:arg=replacement_objects[key(arg)]
 elif Path(arg).is_absolute() and Path(arg).suffix.lower() in ('.obj','.lib','.res'):inputs[key(arg)]=identity(arg)
 link.append(arg)
link+=['/OUT:'+str(out/'lifecycle.exe'),'/PDB:'+str(out/'lifecycle.pdb'),'/SUBSYSTEM:CONSOLE',str(out/'purchase_sdk.obj')]
for src in [out/'overlay.json',Path(__file__),root/'tests/csol-skull2/run_lifecycle.py',a.template/'prepared.json',base/'complete-object-provenance.json']:
 inputs[key(src)]=identity(src)
write(out/'prepared.json',dict(jobs=jobs+[dict(name='purchase_sdk',command=command),dict(name='link',command=link)],
 inputs=list(inputs.values()),environment=read(base/'environment.json'),groups=[str(i) for i in range(80)],
 baseline=str(base),boundary='Actual complete transaction/weapon/SDK objects with controlled engine APIs and real ReAPI hooks. No runtime DLL loading, map/server, HTTP, rendered view, actual network delta or scenario price transaction.'))
print(json.dumps(dict(inputs=len(inputs),objects=len(units),groups=80,compiles=1+len(jobs))))
