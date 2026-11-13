# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual CoF Glock/kick SDK differential over the sealed current full build."""
import argparse,importlib.util,json,os,re,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--seal',type=Path,required=True)
p.add_argument('--lock',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--adapter-dir',type=Path,required=True)
a=p.parse_args()
spec=importlib.util.spec_from_file_location('sdk',ROOT/'tests/csol-rebuy/run_sdk.py')
sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
seal=sdk.read(a.seal);lock=sdk.read(a.lock)
out=sdk.checked(a.output,exists=False)
assert out.is_relative_to(ROOT/'build') and not out.exists()
support=lock['support'];assert len(support)==430
for row in support+[lock['copied'],seal['compiler'],seal['linker'],seal['environment']]:sdk.verify(row)
fixture=ROOT/'tests/csol-cof-kick/sdk.cpp'
host=ROOT/'native/csol/server/scenario/core/human_kick.cpp'
controls=[sdk.identity(a.seal),sdk.identity(a.lock),sdk.identity(Path(__file__)),sdk.identity(fixture),sdk.identity(host)]
controls += [sdk.identity(a.adapter_dir / name) for name in ('engine_edge_names.inc','engine_edge_defaults.inc')]
inventory={str(sdk.checked(x['path'])).casefold():x for x in sdk.read(seal['sourceInventory']['path'])}
inputs={str(sdk.checked(x['path'])).casefold():x for x in controls}
env=os.environ.copy()
for key,value in sdk.read(seal['environment']['path']).items():
 for old in list(env):
  if old.casefold()==key.casefold():del env[old]
 env[key]=value
out.mkdir();products=[]
r=dict(success=False,sourceCopies=0,fullDLL=False,steps=[],support=support,oldHost=lock['copied'],
       controls=controls,deployed=False,gameplay=False,livePerformance=False,
       boundary='Actual Glock class and actual kick entrypoints; controlled Studio/resource/owner/effect transport; no physical keyboard or P345 production claim')
def save():
 r['inputsBefore']=list(inputs.values());r['products']=products
 (out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
def verify():
 for row in support+[lock['copied']]+controls+list(inputs.values())+products:sdk.verify(row)
def run(name,command,compile=False,expect_failure=False):
 verify();r['activeStep']=name;save();started=time.monotonic()
 job=command
 if compile or name.startswith('link'):
  rsp=out/(name+'.rsp');rsp.write_text(subprocess.list2cmdline(command[1:]),encoding='utf8');job=[command[0],'@'+str(rsp)]
 q=subprocess.run(job,cwd=sdk.SDK_CWD,env=env,capture_output=True,timeout=1200)
 text=(q.stdout+q.stderr).decode('utf8','replace');(out/(name+'.log')).write_bytes(q.stdout+q.stderr)
 step=dict(name=name,command=command,exit=q.returncode,seconds=time.monotonic()-started)
 r['steps'].append(step)
 if compile and not q.returncode:
  for inc in re.findall(r'Note: including file:\s*([^\r\n]+)',text):
   path=sdk.checked(sdk.SDK_CWD/inc.strip());row=sdk.identity(path)
   if path.is_relative_to(ROOT)and not path.is_relative_to(ROOT/'build')and not path.is_relative_to(ROOT/'tests'):
    old=inventory.get(str(path).casefold())
    assert old and old['sha256']==row['sha256'],'unrecorded support header drift '+str(path)
   inputs.setdefault(str(path).casefold(),row)
 if not q.returncode and (compile or name.startswith('link')):
  prefix='/fo'if compile else'/out:'
  products.append(sdk.identity(next(x[len(prefix):]for x in command if x.lower().startswith(prefix))))
 if not compile and not name.startswith('link'):
  summary=re.findall(r'^behaviorChecks=(\d+) checks=(\d+) failures=(\d+)\s*$',text,re.M)
  assert len(summary)==1 and int(summary[0][0])>0 and int(summary[0][1])>0
  step['behaviorChecks'],step['sdkChecks'],step['failures']=map(int,summary[0])
  if expect_failure:
   assert q.returncode!=0 and 'FAIL elapsed actual Glock absolute timer enables kick Ready'in text
  else:assert q.returncode==0 and step['failures']==0
 elif q.returncode:raise RuntimeError(name+' failed')
 r.pop('activeStep',None);save();print(name,q.returncode,flush=True)
 if not compile or q.returncode:print(text[-7000:],flush=True)
 return text
try:
 base=seal['compileCommand']
 flags=[x for x in base if not x.lower().endswith('.cpp') and not x.lower().startswith(('/fo','/fp','/yu','/yc','/fd'))]
 if '/fp:precise'not in [x.lower()for x in flags]:flags+=['/fp:precise']
 flags+=['/showIncludes','/clang:-ffp-contract=off','/I'+str(ROOT),'/I'+str(a.adapter_dir.resolve()),
     '/I'+str(ROOT/'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include')]
 run('compile-fixture',flags+['/Fo'+str(out/'fixture.obj'),str(fixture)],True)
 run('compile-new-host',flags+['/Fo'+str(out/'new-human-kick.obj'),str(host)],True)
 remove=('/out:','/pdb:','/implib:','/def:','/ltcgout:','/manifest','/natvis:','/subsystem:','/threads:','/opt:lldltojobs=','/lldltocache:')
 oldShared=Path(lock['original']['path']).name.casefold()
 link=[x for x in seal['linkCommand']if x.lower()!='/dll'and not x.lower().startswith(remove)and Path(x).name.casefold()!=oldShared]
 for mode,obj in [('old',lock['copied']['path']),('new',str(out/'new-human-kick.obj'))]:
  exe=out/(mode+'.exe')
  run('link-'+mode,link+['/SUBSYSTEM:CONSOLE','/OUT:'+str(exe),'/PDB:'+str(out/(mode+'.pdb')),
      '/threads:2','/opt:lldltojobs=2',obj,str(out/'fixture.obj')])
  groups=['ready']if mode=='old'else['ready','completion','reload','aim','attack','ak']
  for group in groups:run(mode+'-'+group,[str(exe),group],expect_failure=mode=='old')
 verify();r['success']=r['inputsUnchanged']=True
except Exception as error:
 r['error']=str(error);raise
finally:save()
