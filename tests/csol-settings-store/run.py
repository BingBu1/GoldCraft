#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build actual independent Windows store/parser and run real filesystem cases."""
import argparse,hashlib,json,os,re,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--environment',type=Path,required=True)
p.add_argument('--compiler',type=Path,required=True)
a=p.parse_args();out=a.output.resolve()
assert out.is_relative_to(ROOT/'build') and not out.exists()
for path in (out,*out.parents):assert not path.is_symlink() and not getattr(path,'is_junction',lambda:False)()
out.mkdir(parents=True)
env=os.environ.copy();env.update(json.loads(a.environment.read_text('utf-8-sig')))
inputs=[ROOT/x for x in ['native/csol/server/custom/settings_store.hpp','native/csol/server/custom/settings_store.cpp',
 'native/csol/scenario/custom/settings.hpp','native/csol/scenario/custom/settings.cpp','tests/csol-settings-store/main.cpp','tests/csol-settings-store/run.py']]
inputs+=list((ROOT/'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include').rglob('*.h'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
identities=[dict(path=str(path),bytes=path.stat().st_size,sha256=sha(path)) for path in inputs]
result=dict(success=False,sourceCopies=0,gitRevision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),inputsBefore=identities,steps=[])
def save():(out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
cmd=[str(a.compiler.resolve()),'/nologo','--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/utf-8',
 '/clang:-O3','/clang:-flto=thin','/arch:AVX2','/fp:precise','/clang:-fuse-ld=lld',
 '/I'+str(ROOT/'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include'),
 str(ROOT/'native/csol/server/custom/settings_store.cpp'),str(ROOT/'native/csol/scenario/custom/settings.cpp'),
 str(ROOT/'tests/csol-settings-store/main.cpp'),'/Fe'+str(out/'settings-store.exe'),'/Fo'+str(out)+'/',
 '/link','/SUBSYSTEM:CONSOLE']
for name,command in [('compile-link',cmd),('execute',[str(out/'settings-store.exe'),str(out/'data')])]:
 start=time.monotonic();q=subprocess.run(command,cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
 (out/(name+'.log')).write_bytes(q.stdout)
 result['steps'].append(dict(name=name,command=command,exit=q.returncode,seconds=time.monotonic()-start));save()
 print(name,q.returncode,flush=True)
 if q.returncode:
  print(q.stdout.decode('utf8',errors='replace'));raise SystemExit(1)
result['changedAfter']=[x['path'] for x in identities if sha(Path(x['path']))!=x['sha256']]
result['inputsUnchanged']=not result['changedAfter'];result['success']=result['inputsUnchanged'];save()
print((out/'execute.log').read_text(),flush=True)
raise SystemExit(0 if result['success'] else 1)
