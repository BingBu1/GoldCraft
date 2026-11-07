#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual inventory and HUD title consumers through controlled SDK APIs."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,time
ROOT=Path(__file__).resolve().parents[2]
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def ident(p):
 p=Path(p).resolve();return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def key(p):return os.path.normcase(str(Path(p).resolve()))
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--evidence',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--reuse',type=Path);ap.add_argument('--rebuild',action='append',default=['client_title_inventory']);a=ap.parse_args()
 baseline=a.baseline.resolve();evidence=ident(a.evidence);proof=read(a.evidence)
 assert proof['module']=='f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd'and proof['failures']==0
 environment_file=baseline/'environment.json';environment_source=ident(environment_file)
 out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir();source=out/'source';(out/'temp').mkdir()
 selected={r['relative']:r['frozen']for r in read(baseline/'effective-source-manifest.json')['files']}
 changed={'native/csol/client/hud_style.cpp','native/csol/client/weapons/weapon_inventory.cpp','native/csol/client/weapons/weapon_inventory.hpp'}
 frozen={};known={key(r['path']):r for r in read(baseline/'inputs.json')['files']}
 def freeze(name):
  if name in frozen:return
  original=ident(ROOT/name)if name in changed or name.startswith('tests/')else selected.get(name)or ident(ROOT/name)
  assert ident(original['path'])==original
  data=Path(original['path']).read_bytes();p=source/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);assert p.stat().st_nlink==1
  row=dict(relative=name,original=original,frozen=ident(p));frozen[name]=row;known[key(p)]=row['frozen']
  for token in re.findall(r'^\s*#include\s+"([^"]+)"',data.decode('utf-8-sig'),re.M):
   local=Path(os.path.normpath(str(Path(name).parent/token))).as_posix()
   child=local if(ROOT/local).is_file()else token
   if(ROOT/child).is_file():freeze(child)
 roots=['tests/csol-skull3/client_title_inventory.cpp','tests/csol-skull3/client_title_hud.cpp','tests/csol-hud-style/switch.cpp','native/csol/hud_style.cpp','native/csol/image_codec.cpp']
 for name in roots:freeze(name)
 base=[];skip=False
 hud_command=next(job['command']for job in read(baseline/'build-commands.json')
     if job['kind']=='compile'and job['command'][-1].replace('\\','/').endswith('/native/csol/client/hud_style.cpp'))
 for arg in hud_command:
  if arg=='--':break
  if skip:skip=False;continue
  if arg=='/clang:-ivfsoverlay':skip=True;continue
  if arg.startswith(('/Fo','/Fd')):continue
  base.append(arg)
 base.append('-I'+str(source));env=read(environment_file)|dict(TEMP=str(out/'temp'),TMP=str(out/'temp'))
 commands=[];objects=[];reused=[]
 if a.reuse:
  old_plan=read(a.reuse/'plan.json');old_result=read(a.reuse/'result.json')
  old_sources={r['relative']:r['frozen']for r in old_plan['sources']}
  for name,row in frozen.items():
   if not(name.startswith('tests/csol-skull3/')and Path(name).stem in a.rebuild):assert row['frozen']['sha256']==old_sources[name]['sha256']and ident(old_sources[name]['path'])==old_sources[name],name
 for name in roots:
  stem=Path(name).stem
  if a.reuse and stem not in a.rebuild:
   previous=next((s for s in old_result['steps']if s['name']==stem),None)
   if previous:assert previous['exitCode']==0;obj=(a.reuse/(stem+'.obj')).resolve()
   else:obj=Path(next(r['path']for r in old_plan['reused']if Path(r['path']).stem==stem))
   reused.append(ident(obj))
  else:
   obj=out/(stem+'.obj');commands.append((stem,[*base,'/Fo'+str(obj),'/Fd'+str(out/(stem+'.compile.pdb')),'--',str(source/name)]))
  objects.append(obj)
 for i in range(3):
  stem=Path(roots[i]).stem;exe=out/(stem+'.exe');objs=[objects[i]]+([]if i==0 else objects[3:])
  commands.append((stem+'-link',[base[0],'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',*[str(p)for p in objs],'/Fe'+str(exe),'user32.lib','gdi32.lib','/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1','/opt:lldltojobs=1']))
  commands.append((stem+'-test',[str(exe)]))
 plan=dict(sources=list(frozen.values()),commands=commands,reused=reused,reusedBuild=ident(a.reuse/'result.json')if a.reuse else None,baseline=ident(baseline/'execution-handoff.json'),evidence=evidence,environmentSource=environment_source,environment=env)
 (out/'plan.json').write_text(json.dumps(plan,indent=2));start=time.monotonic();steps=[];unknown=[];actual={}
 for label,command in commands:
  run=subprocess.run(command,env=os.environ|env,cwd=out,capture_output=True,timeout=90);log=out/(label+'.log');log.write_bytes(run.stdout+run.stderr)
  steps.append(dict(name=label,exitCode=run.returncode,log=ident(log)))
  for p in re.findall(r'^\s*Note: including file:\s*(.+?)\s*$',log.read_text('utf8','replace'),re.M):
   k=key(p)
   if k not in known:unknown.append(p)
   else:assert ident(p)==known[k];actual[k]=known[k]
  print(label,run.returncode,flush=True)
  if run.returncode or unknown:print(log.read_text('utf8','replace')[-6000:]);break
 success=len(steps)==len(commands)and all(s['exitCode']==0 for s in steps)and not unknown
 unchanged=all(ident(r['frozen']['path'])==r['frozen']for r in frozen.values())
 unchanged=unchanged and all(ident(r['path'])==r for r in reused)
 unchanged=unchanged and ident(a.evidence)==evidence and ident(environment_file)==environment_source
 result=dict(success=success and unchanged,steps=steps,actualIncludes=list(actual.values()),unknownInputs=unknown,inputsUnchanged=unchanged,plan=ident(out/'plan.json'),runner=ident(__file__),seconds=time.monotonic()-start,liveGameplay=False)
 (out/'result.json').write_text(json.dumps(result,indent=2));assert result['success']
if __name__=='__main__':main()
