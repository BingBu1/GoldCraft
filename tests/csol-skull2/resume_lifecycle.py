#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze a corrected candidate and reuse only successful unchanged SDK objects."""
import argparse,hashlib,json,os,shutil
from pathlib import Path
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def ident(p):
 p=Path(p).resolve();return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def key(p):return os.path.normcase(str(Path(p).resolve()))
def write(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2),encoding='utf-8')
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--previous',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True,action='append')
p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];old=a.previous.resolve();out=a.output.resolve();out.mkdir()
prepared=read(old/'prepared.json');result=read(old/'result.json');overlay=read(old/'overlay.json')
updates={}
for manifest in a.manifest:
 for row in read(manifest):
  relative=row['path'];src=manifest.resolve().parent/'source'/relative
  assert ident(src)['sha256']==row['sha256']
  dst=out/'source'/relative;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst);updates[relative]=dst
relative='tests/csol-skull2/lifecycle_sdk.cpp';dst=out/'source'/relative;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/relative,dst);updates[relative]=dst
for mapping in overlay['roots']:
 for relative,dst in updates.items():
  if mapping['name'].replace('\\','/').lower().endswith('/'+relative.lower()):mapping['external-contents']=str(dst)
for mapping in list(overlay['roots']):
 name=Path(mapping['name'])
 if name.is_relative_to(old/'source'):
  overlay['roots'].append(dict(type='file',name=str(out/'source'/name.relative_to(old/'source')),
   **{'external-contents':mapping['external-contents']}))
for relative,dst in updates.items():overlay['roots'].append(dict(type='file',name=str(dst),**{'external-contents':str(dst)}))
write(out/'overlay.json',overlay)
passed={x['name'] for x in result['steps'] if x.get('exit')==0 and not x.get('unknown') and not x.get('changed')}
recompile={Path(x).stem for x in updates}
jobs=[];objects={};reused=[]
for job in prepared['jobs']:
 if job['name']=='link':continue
 command=job['command'];output=next(x[3:] for x in command if x.startswith('/Fo'))
 if job['name'] in passed and job['name'] not in recompile:
  reused.append(ident(output));continue
 new=[]
 for x in command:
  if x.startswith('/Fo'):new.append('/Fo'+str(out/Path(output).name))
  elif x.startswith('/clang:') and x.endswith('overlay.json'):new.append('/clang:'+str(out/'overlay.json'))
  elif x.endswith('.cpp'):
   relative=next((r for r in updates if x.replace('\\','/').lower().endswith('/'+r.lower())),None)
   new.append(str(updates[relative]) if relative else x)
  else:new.append(x)
 objects[key(output)]=str(out/Path(output).name)
 jobs.append(dict(name=job['name'],command=new))
# A newly discovered dependency may replace an inherited baseline object.
baseline=Path(prepared['baseline'])
provenance=read(baseline/'complete-object-provenance.json')['units']
existing_names={j['name'] for j in prepared['jobs']}
for relative,source in updates.items():
 if not relative.endswith('.cpp') or Path(relative).stem in existing_names:continue
 matches=[u for u in provenance if u['source']['path'].replace('\\','/').lower().endswith('/'+relative.lower())]
 assert len(matches)==1,relative
 unit=matches[0];assert ident(unit['object']['path'])['sha256']==unit['object']['sha256']
 command=list(next(j['command'] for j in prepared['jobs'] if j['name']=='wpn_skull2'))
 for i,x in enumerate(command):
  if x.startswith('/Fo'):command[i]='/Fo'+str(out/(Path(relative).stem+'.obj'))
  elif x.startswith('/clang:') and x.endswith('overlay.json'):command[i]='/clang:'+str(out/'overlay.json')
  elif x.endswith('.cpp'):command[i]=str(source)
 objects[key(unit['object']['path'])]=str(out/(Path(relative).stem+'.obj'))
 jobs.append(dict(name=Path(relative).stem,command=command))
link=[]
for x in prepared['jobs'][-1]['command']:
 if x.lower().startswith('/out:'):x='/OUT:'+str(out/'lifecycle.exe')
 elif x.lower().startswith('/pdb:'):x='/PDB:'+str(out/'lifecycle.pdb')
 elif x.lower().endswith('.obj') and Path(x).is_absolute():x=objects.get(key(x),x)
 link.append(x)
jobs.append(dict(name='link',command=link))
inputs={key(x['path']):ident(x['path']) for x in prepared['inputs']}
for x in [out/'overlay.json',*a.manifest,Path(__file__),root/'tests/csol-skull2/run_lifecycle.py',*updates.values()]:inputs[key(x)]=ident(x)
for x in reused:inputs[key(x['path'])]=x
prepared.update(jobs=jobs,inputs=list(inputs.values()),resumedFrom=str(old),reusedNewObjects=reused,
 groups=['capacity','reload','drop','pack','reentry'])
write(out/'prepared.json',prepared)
print(json.dumps(dict(jobs=len(jobs),reused=len(reused),inputs=len(inputs))))
