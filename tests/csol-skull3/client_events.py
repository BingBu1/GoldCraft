#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare actual SDK client event calls with an external original-instruction oracle."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,time,math
ROOT=Path(__file__).resolve().parents[2]
def identity(p):
 p=Path(p).resolve();b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--oracle',type=Path);ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--fixture',choices=['events','prediction','bullets','callbacks'],default='events');a=ap.parse_args()
 out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir();(out/'temp').mkdir();source=out/'source';inputs=[]
 paths=list((ROOT/'native/csol').rglob('*.hpp'))+list((ROOT/'native/csol').rglob('*.h'))+list((ROOT/'native/include').rglob('*.h'))
 paths += [ROOT/x for x in ['native/csol/client/skull3.cpp','native/csol/client/shared_random.cpp','native/csol/client/weapon_module.cpp','native/csol/client/gun_smoke.cpp','tests/csol-skull3/client_events.cpp']]
 if a.fixture!='events':paths.append(ROOT/f'tests/csol-skull3/client_{a.fixture}.cpp')
 for p in paths:
  q=source/p.relative_to(ROOT);q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q);assert q.stat().st_nlink==1;inputs.append(dict(original=identity(p),frozen=identity(q)))
 base=json.loads(a.baseline.read_text('utf-8-sig'));env=os.environ|base['environment'];env['TEMP']=env['TMP']=str(out/'temp')
 command=[]
 for arg in base['commands'][0]['argv']:
  if arg=='--':break
  if arg.startswith(('/Fo','/Fd'))or arg=='-c'or '/sdk-01/generated' in arg.replace('\\','/')or arg=='/showIncludes':continue
  command.append(arg)
 command += ['-fuse-ld=lld',str(source/f'tests/csol-skull3/client_{a.fixture}.cpp'),'/Fo'+str(out/'client.obj'),'/Fe'+str(out/'client.exe'),'/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1']
 start=time.monotonic();build=subprocess.run(command,env=env,cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90);(out/'build.log').write_bytes(build.stdout)
 assert build.returncode==0,build.stdout.decode('utf8','replace')
 if a.fixture=='prediction':
  run=subprocess.run([str(out/'client.exe')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30);(out/'run.log').write_bytes(run.stdout)
  observed=json.loads(run.stdout.decode().splitlines()[-1]);changed=[r['original']['path']for r in inputs if identity(r['original']['path'])!=r['original']]
  result=dict(success=not observed['failures']and run.returncode==0,observed=observed,inputs=inputs,changedSinceFreeze=changed,baseline=identity(a.baseline),command=command,seconds=time.monotonic()-start,liveGameplay=False)
  (out/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items()if k not in ('inputs','command')},indent=2));assert result['success'];return
 assert a.oracle is not None
 oracle=json.loads(a.oracle.read_text());assert oracle.get('moduleSha256',oracle.get('module',{}).get('sha256'))=='f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd'
 cases=oracle['cases'];kinds={'0x25b6770':0,'0x25b6b50':1,'0x25b6f10':2}
 if a.fixture=='bullets':
  cases=[r for r in cases if r['entityIndex']==0 and r['solid']==4]
  payload=''.join(f"{r['material']} {r['hitDistance']}\n"for r in cases)
 elif a.fixture=='callbacks':payload=''.join(' '.join(str(v)for v in [r['kind'],r['frame'],*r['velocity'],*r['normal'],*r['wind'],*r['direction'],r['random']])+'\n'for r in cases)
 else:payload=''.join(' '.join(str(int(x))for x in [kinds[r['entry']],r['bparam'],r['local'],r['rightHand'],r['smoke'],r['random']])+'\n'for r in cases)
 run=subprocess.run([str(out/'client.exe')],input=payload.encode(),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30);(out/'run.log').write_bytes(run.stdout)
 actual=[json.loads(line)for line in run.stdout.decode().splitlines()];assert len(actual)==len(cases)
 checks=0;failures=[]
 def compare(expected,observed,path):
  nonlocal checks
  if isinstance(expected,dict):
   for k,v in expected.items():
    if k in ('callback','hitcallback'):continue
    compare(v,observed.get(k),path+'.'+k)
  elif isinstance(expected,list):
   checks+=1
   if not isinstance(observed,list)or len(expected)!=len(observed):failures.append([path,expected,observed]);return
   for i,(x,y)in enumerate(zip(expected,observed)):compare(x,y,path+f'[{i}]')
  else:
   checks+=1
   ok=math.isclose(expected,observed,rel_tol=1e-6,abs_tol=1e-5)if isinstance(expected,(float,int))and isinstance(observed,(float,int))else expected==observed
   if not ok:failures.append([path,expected,observed])
 for i,(expected,observed)in enumerate(zip(cases,actual)):
  if a.fixture=='bullets':
   for key in ['traces','sounds','streaks','decals']:compare(expected.get(key),observed[key],f'{i}.{key}')
   smoke=[dict(source=s['source'],scale=s['scale']*1.25,color=s['color'])for s in expected['smoke']]
   compare(smoke,observed['smoke'],f'{i}.smoke')
   compare([[0,0],[1,0],[2,0],[3,2],[4,0]],observed['engine'],f'{i}.engine')
  elif a.fixture=='callbacks':compare(expected['observed'],observed,f'{i}')
  else:
   for key in ['angles','animation','muzzleFlash','models','rng','shell','sound','sprites']:compare(expected.get(key),observed[key],f'{i}.{key}')
 changed=[r['original']['path']for r in inputs if identity(r['original']['path'])!=r['original']]
 result=dict(success=not failures and run.returncode==0,cases=len(cases),checks=checks,failures=failures,inputs=inputs,changedSinceFreeze=changed,oracle=identity(a.oracle),baseline=identity(a.baseline),command=command,seconds=time.monotonic()-start,liveGameplay=False)
 (out/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items()if k not in ('inputs','command')},indent=2));assert result['success']
if __name__=='__main__':main()
