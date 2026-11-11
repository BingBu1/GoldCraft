#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build in-place x86 smoke helper and compare unchanged original instructions."""
import argparse,hashlib,json,os,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def identity(path):
 path=path.resolve(strict=True)
 with path.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
 return dict(path=str(path),bytes=path.stat().st_size,sha256=digest)
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for name in ('output','compiler','environment','client','evidence'):ap.add_argument('--'+name,type=Path,required=True)
 a=ap.parse_args();out=a.output.resolve()
 if out.exists():raise FileExistsError(out)
 out.mkdir(parents=True);environment=a.environment.resolve(strict=True);compiler=a.compiler.resolve(strict=True);source=Path(__file__).with_name('client.cpp');exe=out/'client.exe'
 paths=[ROOT/'native/csol/scenario/bosses'/n for n in ('teddy_blackhole_smoke.hpp','teddy_spark_particle.hpp','teddy_sparks.hpp')]+[p for p in Path(__file__).parent.iterdir() if p.suffix in ('.py','.cpp')]
 before={str(p.resolve()).casefold():identity(p) for p in paths};controls=[identity(environment),identity(compiler)];env=os.environ|json.loads(environment.read_text('utf8'));env.update(TEMP=str(out),TMP=str(out),VSLANG='1033',PYTHONDONTWRITEBYTECODE='1')
 command=[str(compiler),'/nologo','--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT','/std:c++20','/arch:AVX2','/clang:-O3','/clang:-flto=thin','/fp:precise','/clang:-ffp-contract=off','/EHsc','/showIncludes','/I'+str(ROOT),'/Fo'+str(out/'client.obj'),'/Fe'+str(exe),str(source),'/link','/machine:x86','/OPT:REF','/OPT:ICF','/threads:1']
 result=subprocess.run(command,cwd=ROOT,env=env,text=True,encoding='utf8',errors='replace',capture_output=True);(out/'compile.log').write_text(result.stdout+result.stderr,'utf8')
 included={str(source.resolve()).casefold()}
 for m in re.finditer(r'Note: including file:\s*(.*?)\s*$',result.stdout,re.M):
  path=Path(m.group(1)).resolve()
  if path.is_relative_to(ROOT):included.add(str(path).casefold())
 if included-before.keys():raise AssertionError('Unsealed source dependency '+str(included-before.keys()))
 tests=[]
 if result.returncode==0:
  commands=[[str(exe)],[sys.executable,str(Path(__file__).with_name('oracle.py')),'--executable',str(exe),'--client',str(a.client.resolve(strict=True)),'--evidence',str(a.evidence.resolve(strict=True)),'--output',str(out/'oracle.json')]]
  for i,cmd in enumerate(commands):
   p=subprocess.run(cmd,cwd=ROOT,env=env,text=True,encoding='utf8',errors='replace',capture_output=True,timeout=120);(out/('test'+str(i)+'.log')).write_text(p.stdout+p.stderr,'utf8');tests.append(dict(command=cmd,exit=p.returncode,stdout=p.stdout,stderr=p.stderr))
   if p.returncode:break
 inputs=list(before.values());unchanged=all(identity(Path(x['path']))==x for x in inputs+controls)
 receipt=dict(success=result.returncode==0 and len(tests)==2 and all(t['exit']==0 for t in tests) and unchanged,revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),command=command,compileExit=result.returncode,tests=tests,inputs=inputs,controls=controls,compiledIncludes=[before[k] for k in sorted(included)],inputsUnchanged=unchanged,artifact=identity(exe) if exe.exists() else None,sourceCopies=0,runtimeIntegrated=False,deployed=False,liveGameplay=False,performanceAccepted=False)
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n','utf8');print(json.dumps({k:receipt[k] for k in ('success','compileExit','tests','inputsUnchanged')}));return 0 if receipt['success'] else 1
if __name__=='__main__':raise SystemExit(main())
