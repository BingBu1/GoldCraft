#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare actual C++ Deploy with an externally produced original-instruction oracle."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,time
ROOT=Path(__file__).resolve().parents[2]
def identity(p):
 p=Path(p).resolve();b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--oracle',required=True,type=Path);ap.add_argument('--output',required=True,type=Path);ap.add_argument('--environment',required=True,type=Path);ap.add_argument('--compiler',required=True,type=Path);args=ap.parse_args()
 out=args.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True);(out/'temp').mkdir()
 oracle=json.loads(args.oracle.read_text('utf8'));assert oracle['failed']==0 and oracle['moduleSHA256']=='39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2'
 inputs=[]
 for rel in ['native/csol/weapons/skull3.hpp','tests/csol-skull3/deploy.cpp']:
  src=ROOT/rel;dst=out/'source'/rel;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(src.read_bytes());inputs.append(dict(original=identity(src),frozen=identity(dst)))
 baseline=json.loads(args.environment.read_text('utf-8-sig'));compiler=str(args.compiler.resolve());env=os.environ|baseline.get('environment',baseline);env['TEMP']=env['TMP']=str(out/'temp')
 argv=[compiler,'--target=i686-pc-windows-msvc','/std:c++20','/clang:-O3','/clang:-flto=thin','/arch:AVX2','/fp:precise','/MT','-fuse-ld=lld',str(out/'source/tests/csol-skull3/deploy.cpp'),'/Fo'+str(out/'deploy.obj'),'/Fe'+str(out/'deploy.exe'),'/link','/OPT:LLDLTO=3','/threads:1']
 start=time.monotonic();build=subprocess.run(argv,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90);(out/'build.log').write_bytes(build.stdout)
 assert build.returncode==0,build.stdout.decode('utf8','replace')
 keys=['dual','clip','reserve','last','left','newClip','newReserve','newLeft','sequence','nextAttack','idle']
 data=''.join(' '.join(str(int(r[k]))if isinstance(r[k],bool)else str(r[k])for k in keys)+'\n'for r in oracle['rows']);(out/'input.txt').write_text(data)
 run=subprocess.run([str(out/'deploy.exe')],input=data.encode(),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=15);(out/'run.log').write_bytes(run.stdout);observed=json.loads(run.stdout)
 unchanged=all(identity(x['original']['path'])['sha256']==x['original']['sha256'] and identity(x['frozen']['path'])['sha256']==x['frozen']['sha256']for x in inputs)
 result=dict(success=run.returncode==0 and unchanged,inputs=inputs,oracle=identity(args.oracle),compiler=identity(compiler),argv=argv,buildExit=build.returncode,runExit=run.returncode,result=observed,unchanged=unchanged,seconds=time.monotonic()-start,liveGameplay=False)
 (out/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items()if k not in ['argv','inputs']},indent=2));assert result['success']
if __name__=='__main__':main()
