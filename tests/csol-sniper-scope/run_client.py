#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse,hashlib,json,os,re,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def identity(p):
 p=Path(p).resolve();b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--reuse',type=Path);ap.add_argument('--old-skull5',type=Path);args=ap.parse_args();out=args.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir();(out/'temp').mkdir()
 baseline=read(ROOT/'build/csol-skull5-integration-20261103/sdk-01/proposal.json');env=os.environ|baseline['environment'];env['TEMP']=env['TMP']=str(out/'temp');template=baseline['commands'][0]['argv']
 client=['native/csol/client/'+n+'.cpp'for n in ('session','weapon_inventory','weapons','crosshair','pickup_history','beamgun','cannon','k3','melee','m249ex','balrog11','death_notices','skull1','skull5','weapon_module','gun_smoke')]
 client+=['native/csol/'+n+'.cpp'for n in ('beamgun_state','weapon_slots','melee_attacks')]
 client+=['tests/csol-sniper-scope/client.cpp','tests/csol-weapon-slots/session_edges.cpp']
 todo=list(client);sources={}
 while todo:
  rel=todo.pop()
  if rel in sources:continue
  src=ROOT/rel;selected=args.old_skull5 if args.old_skull5 and rel=='native/csol/client/weapons/skull5.cpp'else src;target=out/'source'/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(selected.read_bytes());sources[rel]=dict(original=identity(src),selected=identity(selected),frozen=identity(target))
  for token in re.findall(r'^\s*#include\s+"([^"]+)"',target.read_text('utf-8-sig'),re.M):
   child=(src.parent/token).resolve()
   if child.is_file()and child.is_relative_to(ROOT)and not child.is_relative_to(ROOT/'external'):todo.append(child.relative_to(ROOT).as_posix())
 (out/'sources.json').write_text(json.dumps(sources,ensure_ascii=False,indent=2),'utf8')
 steps=[];reused=[]
 prior=read(args.reuse/'steps.json')if args.reuse else []
 if args.reuse and(args.reuse/'result.json').exists():prior+=[row['compile']for row in read(args.reuse/'result.json').get('reused',[])]
 def run(name,argv,expected=0):
  start=time.monotonic();r=subprocess.run(argv,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180);log=out/(name+'.log');log.write_bytes(r.stdout);includes=sorted(set(re.findall(r'Note: including file:\s*(.+)',r.stdout.decode('utf8','replace'))))
  steps.append(dict(name=name,argv=argv,exit=r.returncode,expected=expected,seconds=time.monotonic()-start,log=identity(log),includes=[identity(x.strip())for x in includes]));(out/'steps.json').write_text(json.dumps(steps,ensure_ascii=False,indent=2),'utf8');print(name,r.returncode,flush=True)
  if r.returncode!=expected:print(r.stdout.decode('utf8','replace')[-6000:]);raise RuntimeError(name)
 objects=[]
 for i,rel in enumerate(client):
  label=Path(rel).stem+'_'+str(i);obj=out/(label+'.obj');argv=[]
  match=next((s for s in prior if s['name'].startswith('compile-')and s['argv'][-1].replace('\\','/').endswith('/'+rel)and s['exit']==0),None)
  if match:
   previous=Path(match['argv'][-1]);valid=identity(previous)['sha256']==sources[rel]['frozen']['sha256']
   for inc in match['includes']:
    path=Path(inc['path']);logical=next((r for r in sources if path.as_posix().casefold().endswith('/'+r.casefold())),None);selected=out/'source'/logical if logical else path
    valid=valid and selected.is_file()and identity(selected)['sha256']==inc['sha256']
   if valid:
    oldobj=Path(next(a[3:]for a in match['argv']if a.startswith('/Fo')));reused.append(dict(source=rel,object=identity(oldobj),compile=match));objects.append(str(oldobj));print('reuse',rel,flush=True);continue
  for a in template:
   if(a.startswith('/I')or a.startswith('-I'))and'generated'in a:continue
   if a.startswith('/Fo'):a='/Fo'+str(obj)
   elif a.startswith('/Fd'):a='/Fd'+str(out/(label+'.pdb'))
   elif a==template[-1]:a=str(out/'source'/rel)
   argv.append(a)
  run('compile-'+label,argv);objects.append(str(obj))
 exe=out/'client.exe';run('link',[template[0],'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',*objects,'/Fe'+str(exe),'/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1'])
 run('run',[str(exe)],1 if args.old_skull5 else 0)
 changed=[rel for rel,x in sources.items()if identity(x['frozen']['path'])['sha256']!=x['frozen']['sha256']]
 result=dict(success=not changed,sources=sources,steps=steps,reused=reused,changed=changed,runner=identity(__file__),liveGameplay=False,boundary='Full production weapon/inventory/module dispatch and actual SDK types. Engine messages, SPR center, unrelated presentation, metric and scope frame are controlled interfaces. Scope geometry/real GL checked independently.')
 (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8')
if __name__=='__main__':main()
