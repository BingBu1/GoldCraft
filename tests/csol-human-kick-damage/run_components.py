#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual kick objects/fixture over an explicit immutable full02 support snapshot.

Not whole-server acceptance: preserve the original rejected broad receipt.
"""
import argparse,hashlib,importlib.util,json,os,re,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
for name in ['baseline','environment','output','adapter-dir']:p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--with-parent-callsite',action='store_true')
a=p.parse_args()
spec=importlib.util.spec_from_file_location('sdk',ROOT/'tests/csol-rebuy/run_sdk.py');sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
base,out=sdk.checked(a.baseline),sdk.checked(a.output,exists=False);adapter=sdk.checked(a.adapter_dir)
assert base.is_relative_to(ROOT/'build') and out.is_relative_to(ROOT/'build') and not out.exists()
baseline=sdk.read(base/'receipt.json')
assert baseline['artifact']['sha256']=='e98493ddac8a7689ca09f40c66a06861ffdc949aafcc208c4aa5e199101cf247'
assert baseline['artifact']['bytes']==5921280 and all(x['exit']==0 for x in baseline['steps'])
sdk.verify(baseline['artifact'])
environment=sdk.checked(a.environment)
assert any(sdk.checked(x['path'])==environment and sdk.digest(environment)==x['sha256'] for x in baseline['controls'])
compile_template,link=sdk.commands(base/'msbuild.log')
support=sdk.resolve_link_inputs(link,sdk.read(environment));assert len(support)==428
old={str(sdk.checked(x['path'])).casefold():x for x in sdk.read(base/'sources/source-inputs.json')}
header=ROOT/'native/csol/server/scenario/core/human_kick_damage.h'
new=b'csol::scenario::ConfigError& error,\n    bool (*ownerCurrent)(const void*), const void* ownerContext);'
previous=b'csol::scenario::ConfigError& error);'
raw=header.read_bytes();assert raw.count(new)==1
reverse=raw.replace(new,previous)
proof=dict(path=str(header),current=sdk.identity(header),baseline=old[str(header).casefold()],
 change='Only append mandatory synchronous callback/context to one free function declaration; no shared type/ABI layout change',
 reverseSha256=hashlib.sha256(reverse).hexdigest())
assert proof['reverseSha256']==proof['baseline']['sha256'],'header has other changes beyond permitted declaration'
sources={'csol_kick_human_kick_damage.obj':ROOT/'native/csol/server/scenario/core/human_kick_damage.cpp'}
if a.with_parent_callsite:sources['csol_kick_human_kick.obj']=ROOT/'native/csol/server/scenario/core/human_kick.cpp'
fixture=ROOT/'tests/csol-human-kick-damage/sdk.cpp';allowed=set(sources.values())|{fixture}
inputs={str(x).casefold():sdk.identity(x) for x in allowed}
for top in [ROOT/'tests',ROOT/'native/csol/config',adapter]:
 for path in top.rglob('*'):
  if path.is_file() and (top!=ROOT/'tests' or path.suffix.lower() in ['.h','.hpp','.inc','.inl']):inputs[str(path).casefold()]=sdk.identity(path)
controls=[sdk.identity(x) for x in [Path(__file__),ROOT/'tests/csol-rebuy/run_sdk.py',base/'receipt.json',base/'msbuild.log',base/'sources/source-inputs.json',environment,Path(compile_template[0]),Path(link[0])]]
env=os.environ.copy();assert not any(env.get(x) for x in ['CL','_CL_'])
for key,value in sdk.read(environment).items():
 for existing in list(env):
  if existing.casefold()==key.casefold():del env[existing]
 env[key]=value
out.mkdir();generated=[]
r=dict(success=False,inputsUnchanged=False,sourceCopies=0,fullDLL=False,originalBroadReceiptAccepted=False,
 baselineArtifact=baseline['artifact'],supportInputs=support,controls=controls,headerProof=proof,steps=[],
 deployed=False,gameplay=False,performanceAccepted=False)
def save():
 r['inputsBefore']=list(inputs.values());r['generatedArtifacts']=generated
 (out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
def verify():
 for row in support+controls+list(inputs.values())+generated:sdk.verify(row)
def execute(name,command,kind):
 verify();started=time.monotonic()
 job=command
 if kind!='behavior':
  rsp=out/(name+'.rsp');rsp.write_text(subprocess.list2cmdline(command[1:]),encoding='utf8');job=[command[0],'@'+str(rsp)]
 q=subprocess.run(job,cwd=sdk.SDK_CWD,env=env,capture_output=True,timeout=300)
 text=(q.stdout+q.stderr).decode('utf8',errors='replace');(out/(name+'.log')).write_bytes(q.stdout+q.stderr)
 r['steps'].append(dict(name=name,kind=kind,command=command,exit=q.returncode,seconds=time.monotonic()-started))
 if q.returncode==0 and kind in ['compile','link']:
  prefix='/fo' if kind=='compile' else '/out:'
  paths=[x[len(prefix):] for x in command if x.lower().startswith(prefix)];assert len(paths)==1
  generated.append(sdk.identity(paths[0]))
 if kind=='compile' and q.returncode==0:
  for include in re.findall(r'Note: including file:\s*([^\r\n]+)',text):
   path=sdk.checked(sdk.SDK_CWD/include.strip())
   if path.is_relative_to(ROOT) and not path.is_relative_to(ROOT/'build'):
    row=sdk.identity(path)
    if path not in allowed and not path.is_relative_to(ROOT/'tests') and path!=header:
     before=old.get(str(path).casefold())
     assert before and before['sha256']==row['sha256'],'support header changed: '+str(path)
    prior=inputs.setdefault(str(path).casefold(),row);assert prior==row,'input drift '+str(path)
 if kind=='behavior' and q.returncode==0:
  counts=re.findall(r'^checks=(\d+) failures=(\d+)\s*$',text,re.M);assert len(counts)==1 and int(counts[0][0])>0 and int(counts[0][1])==0
  r['steps'][-1]['sdkChecks']=int(counts[0][0])
 save();print(name,q.returncode,flush=True)
 if kind=='behavior' or q.returncode:print(text[-9000:],flush=True)
 if q.returncode:raise RuntimeError(name+' failed')
try:
 save()
 flags=[x for x in compile_template if not(x.lower().endswith(('.cpp','.c')) or x.lower().startswith(('/yu','/yc','/fo','/fd')) or(x.lower().startswith('/fp')and not x.lower().startswith('/fp:')))]
 flags+=['/Y-','/showIncludes','/I'+str(ROOT),'/I'+str(adapter)]
 for n,path in enumerate([*sources.values(),fixture]):execute('compile-'+str(n),[*flags,'/Fo'+str(out/(str(n)+'.obj')),str(path)],'compile')
 remove=('/out:','/pdb:','/implib:','/def:','/ltcgout:','/manifest','/natvis:','/subsystem:','/threads:','/opt:lldltojobs=','/lldltocache:')
 linked=[x for x in link if x.lower()!='/dll' and not x.lower().startswith(remove) and Path(x).name.casefold() not in sources]
 for name in sources:assert len([x for x in support if Path(x['path']).name.casefold()==name])==1
 exe=out/'kick-components.exe'
 linked+=['/SUBSYSTEM:CONSOLE','/OUT:'+str(exe),'/PDB:'+str(out/'kick-components.pdb'),'/threads:1','/opt:lldltojobs=1',*[str(out/(str(n)+'.obj'))for n in range(len(sources)+1)]]
 execute('link',linked,'link')
 for group in ['damage','stagger','death','cancel','reentry','owner-guard']:execute(group,[str(exe),group],'behavior')
 verify();r['success']=r['inputsUnchanged']=True
except Exception as error:r['error']=str(error);raise
finally:save()
