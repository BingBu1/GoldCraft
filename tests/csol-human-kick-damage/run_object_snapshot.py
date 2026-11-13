#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute only the three already compiled mutually consistent kick objects.

Historical component behavior only. No current source is compiled, and changed
headers are explicitly outside this snapshot's acceptance (not ABI-compatible).
"""
import argparse,importlib.util,json,os,re,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-receipt',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
spec=importlib.util.spec_from_file_location('sdk',ROOT/'tests/csol-rebuy/run_sdk.py');sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
old=sdk.read(a.source_receipt);out=sdk.checked(a.output,exists=False)
assert out.is_relative_to(ROOT/'build') and not out.exists()
support=old['supportInputs'];objects=old['generatedArtifacts'];assert len(support)==428 and len(objects)==3
assert all(Path(x['path']).suffix=='.obj' for x in objects)
compiled=[x for x in old['steps'] if x['kind']=='compile'];assert len(compiled)==3 and all(x['exit']==0 for x in compiled)
source_paths=[next(v for v in x['command'] if v.lower().endswith('.cpp')) for x in compiled]
source_rows=[next(row for row in old['inputsBefore'] if str(sdk.checked(row['path'])).casefold()==str(sdk.checked(path)).casefold())for path in source_paths]
controls=old['controls']+[sdk.identity(Path(__file__)),sdk.identity(a.source_receipt)]
for row in support+objects+controls+source_rows:sdk.verify(row)
header_drift=[]
for row in old['inputsBefore']:
 path=Path(row['path'])
 if path.suffix.lower() in ['.h','.hpp','.inc','.inl'] and path.is_file() and sdk.digest(path)!=row['sha256']:
  header_drift.append(dict(path=str(path),compiledSha256=row['sha256'],currentSha256=sdk.digest(path)))
assert len(header_drift)==1 and Path(header_drift[0]['path']).name=='plats.h'
assert header_drift[0]['compiledSha256']=='dca431ada656df065654bd731ef7101da433d33c225c82a4a88958d58e275519'
assert header_drift[0]['currentSha256']=='c5075f044f1ccb1e0ff42322c17e7e3e7c541f9a3a1000cae6728ec70e763aeb'
base=Path(next(x['path'] for x in controls if Path(x['path']).name=='receipt.json')).parent
_,link=sdk.commands(base/'msbuild.log')
environment=next(x for x in controls if Path(x['path']).name=='compiler-env.json');env=os.environ.copy()
for key,value in sdk.read(environment['path']).items():
 for existing in list(env):
  if existing.casefold()==key.casefold():del env[existing]
 env[key]=value
out.mkdir();artifacts=[]
r=dict(success=False,inputsUnchanged=False,compiled=False,sourceCopies=0,fullDLL=False,currentSourceAccepted=False,
 originalBroadReceiptAccepted=False,supportInputs=support,compiledObjects=objects,unchangedKickSources=source_rows,
 controls=controls,headerDriftOutsideSnapshot=header_drift,signatureOnlyProof=old['headerProof'],
 boundary='Historical three-object matching ABI snapshot only. Current CFuncTrain grew m_iszModelEx; no header compatibility or current whole-server acceptance is claimed.',steps=[])
def save():
 r['generatedArtifacts']=artifacts;(out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
def verify():
 for row in support+objects+controls+source_rows+artifacts:sdk.verify(row)
def run(name,command,kind):
 verify();r['activeStep']=name;save();started=time.monotonic();job=command
 if kind=='link':
  rsp=out/'link.rsp';rsp.write_text(subprocess.list2cmdline(command[1:]),encoding='utf8');job=[command[0],'@'+str(rsp)]
 q=subprocess.run(job,cwd=sdk.SDK_CWD,env=env,capture_output=True,timeout=1200)
 raw=q.stdout+q.stderr;text=raw.decode('utf8',errors='replace');(out/(name+'.log')).write_bytes(raw)
 step=dict(name=name,kind=kind,command=command,exit=q.returncode,seconds=time.monotonic()-started);r['steps'].append(step)
 if not q.returncode and kind=='link':artifacts.append(sdk.identity(next(x[5:]for x in command if x.lower().startswith('/out:'))))
 if not q.returncode and kind=='behavior':
  result=re.findall(r'^checks=(\d+) failures=(\d+)\s*$',text,re.M);assert len(result)==1 and int(result[0][0])>0 and int(result[0][1])==0
  step['sdkChecks']=int(result[0][0])
 r.pop('activeStep',None);save();print(name,q.returncode,flush=True)
 if kind=='behavior' or q.returncode:print(text[-9000:],flush=True)
 if q.returncode:raise RuntimeError(name+' failed')
try:
 save()
 replaced={'csol_kick_human_kick.obj','csol_kick_human_kick_damage.obj'}
 remove=('/out:','/pdb:','/implib:','/def:','/ltcgout:','/manifest','/natvis:','/subsystem:','/threads:','/opt:lldltojobs=','/lldltocache:')
 command=[x for x in link if x.lower()!='/dll' and not x.lower().startswith(remove) and Path(x).name.casefold() not in replaced]
 exe=out/'kick-object-snapshot.exe'
 command+=['/SUBSYSTEM:CONSOLE','/OUT:'+str(exe),'/PDB:'+str(out/'kick-object-snapshot.pdb'),'/threads:4','/opt:lldltojobs=4',*[x['path']for x in objects]]
 run('link',command,'link')
 for group in ['damage','stagger','death','cancel','reentry','owner-guard']:run(group,[str(exe),group],'behavior')
 verify();r['success']=r['inputsUnchanged']=True
except Exception as error:r['error']=str(error);raise
finally:save()
