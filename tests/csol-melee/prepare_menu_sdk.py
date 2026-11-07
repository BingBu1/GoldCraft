#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze a complete SDK purchase suite, with only rule-phase readiness controlled."""
import argparse,hashlib,json,os,shutil
from pathlib import Path

def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def row(p):
 p=Path(p).resolve();b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def key(p):return os.path.normcase(str(Path(p).resolve()))
def write(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for name in ('baseline','template','edges','menu','output'):ap.add_argument('--'+name,type=Path,required=True)
 a=ap.parse_args();root=Path(__file__).resolve().parents[2];out=a.output.resolve();base=a.baseline.resolve()
 assert out.is_relative_to(root/'build') and not out.exists();out.mkdir()
 baseline=read(base/'inputs.json')['files'];inputs={key(x['path']):x for x in baseline}
 assert all(row(x['path'])['sha256']==x['sha256'] for x in baseline)
 selected={x['relative']:Path(x['frozen']['path']) for x in read(base/'effective-source-manifest.json')['files']}
 overlay={key(x['name']):x for x in read(base/'source-overlay.json')['roots']}
 fixtures=['tests/csol-melee/menu_sdk.cpp','tests/csol-scenario-monster-skills/trap_fixture.hpp',
  'tests/csol-scenario-rules-session/sdk_engine.hpp','tests/csol-scenario-rules-session/resources.hpp',
  'tests/csol-scenario-map-session/fixtures.hpp','tests/csol-scenario-navigation-loader/support.hpp',
  'tests/csol-scenario-difficulty/support.hpp']
 overrides=['native/csol/buy_menu.cpp','native/csol/buy_menu.hpp','native/csol/server/buy_menu.cpp','native/csol/server/scenario/core/game_rules_gate.cpp']
 captured=[]
 for rel in fixtures+overrides:
  src=root/rel;dst=out/'source'/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
  assert src.read_bytes()==dst.read_bytes() and dst.stat().st_nlink==1 and dst.stat().st_ino!=src.stat().st_ino
  captured.append(dict(relative=rel,original=row(src),frozen=row(dst)));selected[rel]=dst
 # Only the map phase predicate is controlled. All original menu, lifecycle,
 # registry, rules ownership, native SDK and actual ReAPI hook bodies remain.
 gate=selected['native/csol/server/scenario/core/game_rules_gate.cpp'];text=gate.read_text('utf-8-sig')
 start=text.index('bool CSOL_ScenarioCanBuy()');body=text.index('{',start);end=body+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 originalGate=text[start:end]
 text=text[:body]+'{ return CSOL_TestBuyReady(); }'+text[end:]
 text='extern bool CSOL_TestBuyReady();\n'+text;gate.write_text(text,encoding='utf8')
 (out/'controlled-phase-original.txt').write_text(originalGate,encoding='utf8')
 for name in ('engine_edge_names.inc','engine_edge_defaults.inc'):
  rel='tests/csol-scenario-rules-session/'+name;dst=out/'source'/rel;shutil.copy2(a.edges/name,dst);selected[rel]=dst
 for rel,src in selected.items():
  for alias in (root/rel,out/'source'/rel,src):overlay[key(alias)]=dict(type='file',name=str(alias),**{'external-contents':str(src)})
 for x in overlay.values():
  for rel in overrides:
   if x['name'].replace('\\','/').lower().endswith('/'+rel.lower()):x['external-contents']=str(selected[rel])
 write(out/'overlay.json',dict(version=0,**{'case-sensitive':False,'use-external-names':True},roots=list(overlay.values())))
 for p in list(selected.values())+[out/'overlay.json']:inputs[key(p)]=row(p)
 template=read(a.template/'prepared.json');command=list(template['jobs'][0]['command'])
 for x in template['inputs']:
  p=Path(x['path']).resolve()
  if not p.is_relative_to(root):assert row(p)['sha256']==x['sha256'];inputs[key(p)]=x
 def compile(rel,name):
  cmd=list(command)
  for i,arg in enumerate(cmd):
   if arg.lower().endswith('.cpp'):cmd[i]=str(selected[rel])
   elif arg.startswith('/Fo'):cmd[i]='/Fo'+str(out/(name+'.obj'))
   elif arg.startswith('/clang:') and arg.endswith('overlay.json'):cmd[i]='/clang:'+str(out/'overlay.json')
  return dict(name=name,command=cmd)
 jobs=[compile('tests/csol-melee/menu_sdk.cpp','fixture')]
 originalLink=read(base/'build-commands.json')[-1]['command'];replacements={}
 for rel in overrides:
  if not rel.endswith('.cpp'):continue
  name=rel.replace('/','_').removesuffix('.cpp');jobs.append(compile(rel,name))
  # Full builds record the effective source for every link object.
  units=read(base/'complete-object-provenance.json')['units']
  matches=[u for u in units if u['source']['path'].replace('\\','/').lower().endswith('/'+rel.lower())]
  assert len(matches)==1,rel;replacements[key(matches[0]['object']['path'])]=str(out/(name+'.obj'))
 link=[]
 for arg in originalLink:
  if arg.lower()=='/dll' or arg.lower().startswith(('/out:','/pdb:','/implib:','/def:','/ltcgout:','/manifest','/natvis:')):continue
  if key(arg) in replacements:arg=replacements[key(arg)]
  elif Path(arg).is_absolute() and Path(arg).suffix.lower() in ('.obj','.lib','.res'):inputs[key(arg)]=row(arg)
  link.append(arg)
 link+=['/OUT:'+str(out/'menu.exe'),'/PDB:'+str(out/'menu.pdb'),'/SUBSYSTEM:CONSOLE',str(out/'fixture.obj')]
 jobs.append(dict(name='link',command=link))
 (out/'resources').mkdir()
 for src,name in [(a.menu,'menu.json')]+[(root/('native/csol/config/'+n+'_damage.json'),n+'_damage.json') for n in ('dragonsword','skullaxe')]:
  dst=out/'resources'/name;shutil.copy2(src,dst);inputs[key(dst)]=row(dst)
 inputs[key(Path(__file__))]=row(Path(__file__))
 cases=['active-knife','deploy-rejected','add-post-rejected','remove-post-rejected','nested-purchase',
  'death-in-add','respawn-in-add','map-close-in-add','holding-m4','holding-usp','no-knife','no-knife-holding-m4',
  'holding-m4-last-knife','pending-attack-deploy-rollback']
 write(out/'prepared.json',dict(jobs=jobs,inputs=list(inputs.values()),environment=read(base/'environment.json'),groups=list(map(str,range(len(cases)))),caseNames=cases,sources=captured,
  baseline=str(base),boundary='Actual complete SDK menu/service/weapon/inventory/account objects and real ReAPI callbacks. Only scenario phase CanBuy predicate replaced in an isolated rules TU; engine APIs controlled. No full-map readiness, runtime deployment, network, GPU or gameplay acceptance.'))
 print(json.dumps(dict(inputs=len(inputs),jobs=len(jobs),output=str(out))))
if __name__=='__main__':main()
