# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import argparse,hashlib,json,os
parser=argparse.ArgumentParser(description='Prepare three actual SDK zombie-bomb protection compilation units; no compilation.')
parser.add_argument('--baseline',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2];base=args.baseline.resolve();out=args.output.resolve()
assert base.is_relative_to(root/'build') and out.is_relative_to(root/'build')
assert not out.exists();out.mkdir()
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def row(p):
 p=p.resolve();b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
ledger=read(base/'effective-source-manifest.json')['files'];overlay=read(base/'source-overlay.json')
names=['scenario_player_runtime.h','scenario_player_runtime.cpp','scenario_weapon_state.h','scenario_weapon_state.cpp','scenario_player_bomb_protection.cpp']
delta=[];mapping={}
for n in names:
 rel='native/csol/server/'+n;src=root/rel;dest=out/'source'/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(src.read_bytes());assert dest.stat().st_nlink==1
 delta.append(dict(relative=rel,original=row(src),frozen=row(dest)))
 mapping[str(src).lower()]=str(dest)
 for x in ledger:
  if x['relative']==rel:mapping[str(Path(x['frozen']['path'])).lower()]=str(dest)
for x in overlay['roots']:
 if x['name'].lower() in mapping:x['external-contents']=mapping[x['name'].lower()]
for path,dest in mapping.items():
 if not any(x['name'].lower()==path for x in overlay['roots']):overlay['roots'].append({'type':'file','name':path,'external-contents':dest})
for x in ledger:
 if Path(x['relative']).suffix.lower() in ('.h','.hpp','.inc'):
  dest=next((r['frozen']['path']for r in delta if r['relative']==x['relative']),x['frozen']['path'])
  overlay['roots'].append({'type':'file','name':str(out/'source'/x['relative']),'external-contents':dest})
save(out/'overlay.json',overlay);save(out/'source-delta.json',delta)
template=next(x['command']for x in read(base/'build-commands.json') if x['name'].startswith('server-scenario_player_runtime'))
flags=[x for x in template if not x.lower().endswith('.cpp') and not x.startswith(('/Fo','/Fd')) and x not in ['/JMC','/Zi']]
flags=[('/clang:'+str(out/'overlay.json')) if x.startswith('/clang:') and x.endswith('source-overlay.json') else x for x in flags]
commands=[dict(name=Path(n).stem,argv=flags+[str(out/'source/native/csol/server'/n),'/Fo'+str(out/(Path(n).stem+'.obj'))])for n in names if n.endswith('.cpp')]
known={}
for r in read(base/'inputs.json')['files']+[x['frozen'] for x in ledger]+[x['frozen']for x in delta]+[row(out/'overlay.json')]:
 p=Path(r['path']);cur=row(p)
 if cur['sha256']==r['sha256']:known[str(p.resolve()).lower()]=cur
save(out/'prepared.json',dict(commands=commands,environment=read(base/'environment.json'),inputs=list(known.values()),delta=delta,base=str(base),jobs=1))
print(json.dumps(dict(out=str(out),inputs=len(known),commands=len(commands))))
