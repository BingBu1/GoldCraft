#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reuse the explicit unchanged current kick objects; compile only a new fixture if requested."""
import argparse,importlib.util,json,os,re,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-receipt',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--fixture',type=Path,required=True);p.add_argument('--groups',required=True)
p.add_argument('--rebuild-host',action='store_true');p.add_argument('--unused-class-proof',type=Path);p.add_argument('--reuse-fixture',action='store_true');p.add_argument('--jobs',type=int,default=4)
a=p.parse_args();assert 1<=a.jobs<=4
spec=importlib.util.spec_from_file_location('sdk',ROOT/'tests/csol-rebuy/run_sdk.py');sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
previous=sdk.read(a.source_receipt);out=sdk.checked(a.output,exists=False);fixture=sdk.checked(a.fixture)
assert out.is_relative_to(ROOT/'build') and not out.exists() and fixture.is_relative_to(ROOT/'tests')
support=previous['supportInputs'];assert len(support)==428
sources=previous['inputsBefore'];oldcontrols=previous['controls'];objects=previous['generatedArtifacts'][:3]
assert len(objects)==3 and all(Path(x['path']).suffix=='.obj' for x in objects)
proof=sdk.read(a.unused_class_proof) if a.unused_class_proof else None
for row in support+oldcontrols+objects:sdk.verify(row)
for row in sources:
 path=sdk.checked(row['path'])
 if path==fixture and not a.reuse_fixture:continue
 if a.rebuild_host and path.name=='human_kick.cpp':continue
 if proof and any(x['path']==str(path) and x['currentSha256']==sdk.digest(path) and x['reverseSha256']==row['sha256'] for x in proof.get('declarationProofs',[])):continue
 if proof and path.name=='plats.h':
  assert row['sha256']==proof['platsCompiledSha256'] and sdk.digest(path)==proof['platsCurrentSha256']
 else:sdk.verify(row)
if proof:
 assert proof['success'] and proof['unusedChangedClass']=='CFuncTrain' and proof['fixtureRecordLayoutCFuncTrainMentions']==0
 for item in proof['commands']:
  assert item['CFuncTrainMentions']==0 and sdk.digest(item['ir'])==item['irSha256']
  assert sdk.digest(item['source'])==item['currentSourceSha256']
 assert any(sdk.checked(item['source'])==fixture for item in proof['commands'])

base=Path(next(x['path'] for x in oldcontrols if Path(x['path']).name=='receipt.json')).parent
old_headers={str(sdk.checked(x['path'])).casefold():x for x in sdk.read(base/'sources/source-inputs.json')}
compile_template,link=sdk.commands(base/'msbuild.log');environment=next(x for x in oldcontrols if Path(x['path']).name=='compiler-env.json')
env=os.environ.copy()
for key,value in sdk.read(environment['path']).items():
 for existing in list(env):
  if existing.casefold()==key.casefold():del env[existing]
 env[key]=value
inputs={str(sdk.checked(x['path'])).casefold():x for x in sources};
if proof:
 for key,row in list(inputs.items()):
  if Path(row['path']).name=='plats.h' or any(x['path']==row['path'] for x in proof.get('declarationProofs',[])) or (a.rebuild_host and Path(row['path']).name=='human_kick.cpp'):inputs[key]=sdk.identity(row['path'])
inputs[str(fixture).casefold()]=sdk.identity(fixture)
controls=[sdk.identity(x) for x in [Path(__file__),a.source_receipt]]
controls+=oldcontrols
if proof:controls+=[sdk.identity(a.unused_class_proof)]+[sdk.identity(item['ir'])for item in proof['commands']]
out.mkdir();generated=[]
r=dict(success=False,inputsUnchanged=False,sourceCopies=0,fullDLL=False,originalBroadReceiptAccepted=False,
 supportInputs=support,reusedKickObjects=objects[:2],reusedFixture=bool(a.reuse_fixture),headerProof=previous['headerProof'],
 controls=controls,fixture=str(fixture),steps=[],deployed=False,gameplay=False,currentSourceAccepted=False,limitedUnusedClassProof=proof)
def save():
 r['inputsBefore']=list(inputs.values());r['generatedArtifacts']=generated
 (out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
def verify():
 for row in support+list(inputs.values())+controls+objects+generated:sdk.verify(row)
def execute(name,command,kind):
 verify();r['activeStep']=name;save();started=time.monotonic();job=command
 if kind!='behavior':
  rsp=out/(name+'.rsp');rsp.write_text(subprocess.list2cmdline(command[1:]),encoding='utf8');job=[command[0],'@'+str(rsp)]
 q=subprocess.run(job,cwd=sdk.SDK_CWD,env=env,capture_output=True,timeout=1200)
 raw=q.stdout+q.stderr;text=raw.decode('utf8',errors='replace');(out/(name+'.log')).write_bytes(raw)
 step=dict(name=name,kind=kind,command=command,exit=q.returncode,seconds=time.monotonic()-started);r['steps'].append(step)
 if not q.returncode and kind in ['compile','link']:
  prefix='/fo' if kind=='compile' else '/out:'
  generated.append(sdk.identity(next(x[len(prefix):] for x in command if x.lower().startswith(prefix))))
 if not q.returncode and kind=='compile':
  header=Path(previous['headerProof']['path'])
  for include in re.findall(r'Note: including file:\s*([^\r\n]+)',text):
   path=sdk.checked(sdk.SDK_CWD/include.strip())
   if path.is_relative_to(ROOT) and not path.is_relative_to(ROOT/'build'):
    row=sdk.identity(path)
    if not path.is_relative_to(ROOT/'tests') and path!=header:
     before=old_headers.get(str(path).casefold())
     unused=proof and path.name=='plats.h' and before and before['sha256']==proof['platsCompiledSha256'] and row['sha256']==proof['platsCurrentSha256']
     declared=proof and before and any(item['path']==str(path) and item['currentSha256']==row['sha256'] and item['baselineSha256']==before['sha256'] and item['reverseSha256']==before['sha256']for item in proof.get('declarationProofs',[]))
     assert unused or declared or (before and before['sha256']==row['sha256']),'support ABI/header drift '+str(path)
    prior=inputs.setdefault(str(path).casefold(),row);assert prior==row
 if not q.returncode and kind=='behavior':
  count=re.findall(r'(?:behaviorChecks=(\d+) )?checks=(\d+) failures=(\d+)\s*$',text,re.M)
  assert len(count)==1 and int(count[0][1])>0 and int(count[0][2])==0,'missing successful SDK count'
  step['sdkChecks']=int(count[0][1]);step['behaviorChecks']=int(count[0][0]) if count[0][0] else None
 r.pop('activeStep',None);save();print(name,q.returncode,flush=True)
 if q.returncode or kind=='behavior':print(text[-9000:],flush=True)
 if q.returncode:raise RuntimeError(name+' failed')
try:
 save()
 fixture_object=objects[2]['path']
 host_object=objects[1]['path']
 if a.rebuild_host:
  flags=[x for x in previous['steps'][1]['command'] if not x.lower().endswith('.cpp') and not x.lower().startswith('/fo')]
  host_source=next(x for x in previous['steps'][1]['command'] if x.lower().endswith('.cpp'))
  host_object=str(out/'host.obj');execute('compile-host',flags+['/Fo'+host_object,host_source],'compile')
 if not a.reuse_fixture:
  flags=[x for x in previous['steps'][2]['command'] if not x.lower().endswith('.cpp') and not x.lower().startswith('/fo')]
  fixture_object=str(out/'fixture.obj');execute('compile-fixture',flags+['/Fo'+fixture_object,str(fixture)],'compile')
 else:assert any(x['path']==str(fixture) for x in sources),'reuse fixture must match original source'
 replaced={'csol_kick_human_kick.obj','csol_kick_human_kick_damage.obj'}
 remove=('/out:','/pdb:','/implib:','/def:','/ltcgout:','/manifest','/natvis:','/subsystem:','/threads:','/opt:lldltojobs=','/lldltocache:')
 command=[x for x in link if x.lower()!='/dll' and not x.lower().startswith(remove) and Path(x).name.casefold() not in replaced]
 exe=out/'kick-components.exe'
 command+=['/SUBSYSTEM:CONSOLE','/OUT:'+str(exe),'/PDB:'+str(out/'kick-components.pdb'),'/threads:'+str(a.jobs),'/opt:lldltojobs='+str(a.jobs),objects[0]['path'],host_object,fixture_object]
 r['actualLinkedKickObjects']=[objects[0],sdk.identity(host_object)];execute('link',command,'link')
 for group in a.groups.split(','):execute(group,[str(exe),group],'behavior')
 verify();r['inputsUnchanged']=r['success']=True
except Exception as error:r['error']=str(error);raise
finally:save()
