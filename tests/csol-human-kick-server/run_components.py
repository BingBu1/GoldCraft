#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile only the controller fixture over the sealed verified kick pair."""
import argparse,importlib.util,json,os,re,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
for name in ['source-receipt','unused-class-proof','output']:p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args()
spec=importlib.util.spec_from_file_location('sdk',ROOT/'tests/csol-rebuy/run_sdk.py');sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
previous=sdk.read(a.source_receipt);proof=sdk.read(a.unused_class_proof);assert previous['success']and previous['inputsUnchanged']and proof['success']
fixture=ROOT/'tests/csol-human-kick-server/sdk.cpp';out=sdk.checked(a.output,exists=False)
assert out.is_relative_to(ROOT/'build')and not out.exists()
support=previous['supportInputs'];kick=previous['actualLinkedKickObjects'];assert len(support)==428 and len(kick)==2
controls=previous['controls']+[sdk.identity(a.source_receipt),sdk.identity(a.unused_class_proof),sdk.identity(Path(__file__))]
for item in proof['commands']:
 assert item['CFuncTrainMentions']==0 and sdk.digest(item['source'])==item['currentSourceSha256'] and sdk.digest(item['ir'])==item['irSha256']
 controls.append(sdk.identity(item['ir']))
for row in support+kick+controls+previous['inputsBefore']:sdk.verify(row)
base=Path(next(x['path']for x in controls if Path(x['path']).name=='msbuild.log')).parent
old={str(sdk.checked(x['path'])).casefold():x for x in sdk.read(base/'sources/source-inputs.json')}
_,link=sdk.commands(base/'msbuild.log')
environment=next(x for x in controls if Path(x['path']).name=='compiler-env.json');env=os.environ.copy()
for key,value in sdk.read(environment['path']).items():
 for existing in list(env):
  if existing.casefold()==key.casefold():del env[existing]
 env[key]=value
inputs={str(sdk.checked(x['path'])).casefold():x for x in previous['inputsBefore']};inputs[str(fixture).casefold()]=sdk.identity(fixture)
out.mkdir();generated=[]
r=dict(success=False,inputsUnchanged=False,sourceCopies=0,fullDLL=False,currentSourceAccepted=False,originalBroadReceiptAccepted=False,
 supportInputs=support,kickObjects=kick,controls=controls,limitedUnusedClassProof=proof,steps=[],deployed=False,gameplay=False,
 boundary='Only controller/damage components over historical SDK support. No cold JSON or AMXX adapter publication is asserted.')
def save():
 r['inputsBefore']=list(inputs.values());r['generatedArtifacts']=generated;(out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
def verify():
 for row in support+kick+controls+list(inputs.values())+generated:sdk.verify(row)
def execute(name,command,kind):
 verify();r['activeStep']=name;save();started=time.monotonic();job=command
 if kind!='behavior':
  rsp=out/(name+'.rsp');rsp.write_text(subprocess.list2cmdline(command[1:]),encoding='utf8');job=[command[0],'@'+str(rsp)]
 q=subprocess.run(job,cwd=sdk.SDK_CWD,env=env,capture_output=True,timeout=1200)
 text=(q.stdout+q.stderr).decode('utf8',errors='replace');(out/(name+'.log')).write_bytes(q.stdout+q.stderr)
 step=dict(name=name,kind=kind,command=command,exit=q.returncode,seconds=time.monotonic()-started);r['steps'].append(step)
 if q.returncode==0 and kind in ['compile','link']:
  prefix='/fo'if kind=='compile'else'/out:';generated.append(sdk.identity(next(x[len(prefix):]for x in command if x.lower().startswith(prefix))))
 if q.returncode==0 and kind=='compile':
  for include in re.findall(r'Note: including file:\s*([^\r\n]+)',text):
   path=sdk.checked(sdk.SDK_CWD/include.strip())
   if path.is_relative_to(ROOT)and not path.is_relative_to(ROOT/'build'):
    row=sdk.identity(path)
    if not path.is_relative_to(ROOT/'tests'):
     before=old.get(str(path).casefold())
     unused=before and path.name=='plats.h'and before['sha256']==proof['platsCompiledSha256']and row['sha256']==proof['platsCurrentSha256']
     declared=before and any(x['path']==str(path)and x['currentSha256']==row['sha256']and x['reverseSha256']==before['sha256']for x in proof.get('declarationProofs',[]))
     guard=previous['headerProof'];signature=path==Path(guard['path'])and row['sha256']==guard['current']['sha256']and before and before['sha256']==guard['reverseSha256']
     assert unused or declared or signature or(before and before['sha256']==row['sha256']),'support header drift '+str(path)
    existing=inputs.setdefault(str(path).casefold(),row);assert existing==row
 if q.returncode==0 and kind=='behavior':
  count=re.findall(r'^behaviorChecks=(\d+) checks=(\d+) failures=(\d+)\s*$',text,re.M)
  assert len(count)==1 and int(count[0][0])>0 and int(count[0][1])>0 and int(count[0][2])==0
  step['behaviorChecks'],step['sdkChecks']=map(int,count[0][:2])
 r.pop('activeStep',None);save();print(name,q.returncode,flush=True)
 if kind=='behavior'or q.returncode:print(text[-9000:],flush=True)
 if q.returncode:raise RuntimeError(name+' failed')
try:
 save();template=next(x['command']for x in previous['steps']if x['name']=='compile-fixture')
 flags=[x for x in template if not x.lower().endswith('.cpp')and not x.lower().startswith('/fo')]
 execute('compile-fixture',flags+['/Fo'+str(out/'fixture.obj'),str(fixture)],'compile')
 remove=('/out:','/pdb:','/implib:','/def:','/ltcgout:','/manifest','/natvis:','/subsystem:','/threads:','/opt:lldltojobs=','/lldltocache:')
 replaced={'csol_kick_human_kick.obj','csol_kick_human_kick_damage.obj'}
 command=[x for x in link if x.lower()!='/dll'and not x.lower().startswith(remove)and Path(x).name.casefold()not in replaced]
 exe=out/'kick-server-components.exe'
 command+=['/SUBSYSTEM:CONSOLE','/OUT:'+str(exe),'/PDB:'+str(out/'kick-server-components.pdb'),'/threads:4','/opt:lldltojobs=4',*[x['path']for x in kick],str(out/'fixture.obj')]
 execute('link',command,'link')
 for group in ['cold','state','completion','deploy-failure','targets','blocked','cancel','callback-cancel','new-life','flood','policy']:execute(group,[str(exe),group],'behavior')
 verify();r['success']=r['inputsUnchanged']=True
except Exception as error:r['error']=str(error);raise
finally:save()
