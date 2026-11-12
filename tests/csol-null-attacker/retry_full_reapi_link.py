#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Preserve full build's first failure, adapt only MSVC .def SECTIONS for lld."""
import sys
sys.dont_write_bytecode=True
import json,hashlib,os,subprocess,time,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=Path(sys.argv[1]).resolve();CWD=ROOT/'external/ReAPI/reapi/msvc'
def ident(p):p=Path(p).resolve();b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def sha(p):return ident(p)['sha256']
p=OUT/'receipt.json';first=json.loads(p.read_text(encoding='utf8'));assert not first['success'] and first['inputsUnchanged'] and len(first['steps'])==35 and all(x['exit']==0 for x in first['steps'][:34])
backup=OUT/'receipt-first-failure.json';backup.write_bytes(p.read_bytes())
inputs={r['path'].casefold():r for r in first['inputsBefore']}
for r in inputs.values():assert sha(r['path'])==r['sha256']
for q in (Path(__file__),backup):inputs[str(q.resolve()).casefold()]=ident(q)
original=CWD/'reapi.def';text=original.read_text();head,sections=text.split('SECTIONS',1);assert sections.split()==['.data','READ','WRITE'];adapt=OUT/'adapter/reapi-lld.def';adapt.write_text(head,encoding='utf8');inputs[str(adapt).casefold()]=ident(adapt)
# Every real producer OBJ/resource is checked and hashed before relink.
for q in list((OUT/'obj').glob('*.obj'))+[OUT/'reapi.res']:inputs[str(q).casefold()]=ident(q)
assert len(list((OUT/'obj').glob('*.obj')))==33
link=first['steps'][-1]['command'];link=[('/DEF:'+str(adapt)) if x.lower().startswith('/def:') else x for x in link]+['/SECTION:.data,RW']
result={k:v for k,v in first.items() if k not in ('inputsBefore','inputsAfter','steps','error','changedInputs','activeProcess')};result.update(success=False,firstFailure=ident(backup),recoveredToolCompatibility=True,adapter=dict(original=ident(original),generated=ident(adapt),transformation='Only remove MSVC SECTIONS .data READ WRITE; preserve original LIBRARY/EXPORTS GiveFnptrsToDll@1 and express data flags with lld /SECTION:.data,RW'),steps=first['steps'],inputsBefore=list(inputs.values()))
env=dict(os.environ)
for key,value in json.loads((ROOT/'build/csol-season2-trace-20261109/compiler-env.json').read_text()).items():
 for old in list(env):
  if old.casefold()==key.casefold():del env[old]
 env[key]=value
env['TEMP']=env['TMP']=str(OUT/'temp')
def save():p.write_text(json.dumps(result,indent=2)+'\n')
def job(name,command):
 rsp=OUT/(name+'.rsp');rsp.write_text(subprocess.list2cmdline(command[1:]),encoding='utf8');log=OUT/(name+'.log');start=time.monotonic()
 with log.open('wb') as f:
  process=subprocess.Popen(command if name=='inspect' else [command[0],'@'+str(rsp)],env=env,cwd=CWD,stdout=f,stderr=subprocess.STDOUT);result['activeProcess']=dict(name=name,pid=process.pid);save();print(json.dumps(dict(start=name,pid=process.pid)),flush=True);exit=process.wait(timeout=180)
 result.pop('activeProcess',None);result['steps'].append(dict(name=name,command=command,pid=process.pid,exit=exit,seconds=time.monotonic()-start,log=ident(log)));save();print(json.dumps(dict(end=name,exit=exit)),flush=True);assert exit==0
try:
 job('link-retry',link);dll=OUT/'reapi_amxx.dll';job('inspect',[str(Path(link[0]).with_name('llvm-readobj.exe')),'--file-headers','--coff-exports','--coff-imports',str(dll)])
 b=dll.read_bytes();nt=struct.unpack_from('<I',b,60)[0];machine=struct.unpack_from('<H',b,nt+4)[0];magic=struct.unpack_from('<H',b,nt+24)[0];assert machine==0x14c and magic==0x10b
 text=(OUT/'inspect.log').read_text();required=['AMXX_Attach','AMXX_Query','AMXX_Detach','Meta_Query','Meta_Attach','Meta_Detach','GiveFnptrsToDll'];assert all(x in text for x in required)
 result['artifact']=ident(dll);result['pe']=dict(machine='I386',machineValue=machine,optionalMagic='PE32',optionalMagicValue=magic,requiredExportsVerified=required);result['success']=True
except Exception as ex:result['error']=str(ex);print(str(ex),flush=True)
finally:
 result['changedInputs']=[r['path'] for r in inputs.values() if not Path(r['path']).is_file() or sha(r['path'])!=r['sha256']];result['inputsUnchanged']=not result['changedInputs'];result['success']&=result['inputsUnchanged'];result['inputsAfter']=[ident(r['path']) for r in inputs.values() if Path(r['path']).is_file()];save()
print(json.dumps(dict(success=result['success'],sourceCount=33,inputsUnchanged=result['inputsUnchanged'],artifact=result.get('artifact'))),flush=True);raise SystemExit(0 if result['success'] else 1)
