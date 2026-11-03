# SPDX-License-Identifier: GPL-3.0-or-later
"""Retain a failed checkpoint and project explicit source changes into a new one."""
from pathlib import Path
import argparse,hashlib,json
p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--replace',action='append',default=[]);a=p.parse_args();root=Path(__file__).resolve().parents[2]
base=a.base.resolve();out=a.output.resolve();assert out.is_relative_to(root/'build') and not out.exists();out.mkdir()
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def row(p):
 p=p.resolve();b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
plan=read(base/'prepared.json');overlay=read(base/'overlay.json');changes={};delta=[]
for rel in a.replace:
 src=root/rel;target=out/'source'/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(src.read_bytes());assert target.stat().st_nlink==1
 old=plan['files'][rel];new=row(target);plan['files'][rel]=new;changes[str(root/rel).lower()]=str(target);changes[old['path'].lower()]=str(target)
 delta.append(dict(relative=rel,previous=old,frozen=new));plan['inputs'].append(new)
for item in overlay['roots']:
 if item['name'].lower() in changes:item['external-contents']=changes[item['name'].lower()]
 elif item['external-contents'].lower() in changes:item['external-contents']=changes[item['external-contents'].lower()]
for old,new in changes.items():
 if not any(x['name'].lower()==old for x in overlay['roots']):overlay['roots'].append(dict(type='file',name=old,**{'external-contents':new}))
# New source files may include siblings relative to their new physical directory.
for rel,r in plan['files'].items():
 if Path(rel).suffix.lower()in('.h','.hpp','.inc','.cpp'):
  overlay['roots'].append(dict(type='file',name=str(out/'source'/rel),**{'external-contents':r['path']}))
save(out/'overlay.json',overlay);plan['inputs'].append(row(out/'overlay.json'))
for c in plan['commands']:
 argv=[]
 for x in c['argv']:
  if x.startswith('/clang:')and x.endswith('overlay.json'):x='/clang:'+str(out/'overlay.json')
  elif x.startswith(('/Fo','/Fe')):x=x[:3]+str(out/Path(x[3:]).name)
  elif x.lower().endswith(('.obj','.exe'))and str(base).lower()in x.lower():x=str(out/Path(x).name)
  elif x.lower()in changes:x=changes[x.lower()]
  argv.append(x)
 c['argv']=argv
plan['retryOf']=str(base);save(out/'source-delta.json',delta);save(out/'prepared.json',plan)
print(str(out))
