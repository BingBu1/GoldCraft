# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual client SDK cylinder consumer against its complete original branch."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
from oracle import generate
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('sample','environment','output'):p.add_argument('--'+name,type=Path,required=True)
 a=p.parse_args();out=a.output.absolute()
 if out.exists() or not out.resolve().is_relative_to(ROOT/'build'):raise ValueError('Use fresh workspace build output')
 if any(x.is_symlink() or os.path.isjunction(x) for x in (out,*out.parents)):raise ValueError('Linked output')
 out.mkdir(parents=True)
 with (out/'cases.txt').open('w') as cases,(out/'expected.txt').open('w') as expected:original=generate(a.sample,cases,expected)
 sdk=ROOT/'external/MetaHookSv-20261007/MetaHook/include'
 includes=[sdk,sdk/'Interface',*[sdk/'HLSDK'/s for s in ('cl_dll','common','engine','pm_shared','public')],sdk/'SourceSDK',sdk/'SourceSDK/tier0',sdk/'SourceSDK/tier1',ROOT]
 config=ROOT/'.tools/clang-toolchain.json';compiler=Path(json.loads(config.read_text('utf-8-sig'))['root'])/'bin/clang-cl.exe'
 sources=[Path(__file__).with_name('main.cpp'),ROOT/'native/guest/cof/client/revolver_cylinder.cpp']
 inputs={*sources,Path(__file__).resolve(),Path(__file__).with_name('oracle.py'),ROOT/'tests/cryoffear-aim/oracle.py',a.sample.resolve(),a.environment.resolve(),config,compiler,compiler.with_name('lld-link.exe')}
 flags=['/nologo','--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/utf-8','/O2','/clang:-O3','/clang:-flto=thin','/clang:-fuse-ld=lld','/arch:AVX2','/fp:precise','/clang:-ffp-contract=off','/DNOMINMAX','/DNO_MALLOC_OVERRIDE','/D_CRT_SECURE_NO_WARNINGS',*('/I'+str(s) for s in includes)]
 env=os.environ|json.loads(a.environment.read_text('utf-8-sig'));temp=out/'temp';temp.mkdir();env.update(TEMP=str(temp),TMP=str(temp))
 commands=[]
 for i,source in enumerate(sources):
  dep=out/f'{i}.d';cmd=[str(compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),str(source)];commands.append(cmd)
  run=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,timeout=60);(out/f'{i}-headers.log').write_text(run.stdout+run.stderr,'utf8');run.check_returncode()
  make=dep.read_text().replace(chr(92)+chr(10),' ')
  inputs.update(Path(v.replace(chr(92)+' ',' ')).resolve() for v in re.findall(r'(?:\\ |[^\s])+',make.split(':',1)[1]))
 before={str(v):sha(v) for v in sorted(inputs)};exe=out/'revolver-cylinder.exe';cmd=[str(compiler),*flags,*map(str,sources),'/Fe:'+str(exe),'/link','/machine:x86','/threads:1','/opt:ref']
 built=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,timeout=120);(out/'compile.log').write_text(built.stdout+built.stderr,'utf8');tested=None;negative=[]
 if built.returncode==0:
  tested=subprocess.run([str(exe),str(out/'cases.txt'),str(out/'expected.txt')],cwd=out,capture_output=True,text=True,timeout=30);(out/'test.log').write_text(tested.stdout+tested.stderr,'utf8')
  if tested.returncode==0:
   good=(out/'expected.txt').read_text()
   for name,cases,results in [('empty','',good),('bad-result',(out/'cases.txt').read_text(),('00000001'if good.split(' ',1)[0]=='00000000'else'00000000')+' '+good.split(' ',1)[1])]:
    c=out/(name+'-cases.txt');e=out/(name+'-expected.txt');c.write_text(cases);e.write_text(results)
    result=subprocess.run([str(exe),str(c),str(e)],cwd=out,capture_output=True,text=True,timeout=30);negative.append({'name':name,'passed':result.returncode!=0,'code':result.returncode})
 after={str(v):sha(v) for v in sorted(inputs)};success=built.returncode==0 and tested is not None and tested.returncode==0 and before==after and all(x['passed'] for x in negative)
 receipt={'success':success,'original':original,'command':cmd,'dependencyCommands':commands,'before':before,'after':after,'unchanged':before==after,'compiled':built.returncode,'tested':tested.returncode if tested else None,'negative':negative,'sourceCopies':0,'boundary':'Actual SDK cylinder consumer with original complete x86 handler; engine lookup/RNG/mixer and resource readiness controlled; no hook/network/render/gameplay/performance acceptance'}
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n','utf8');print(tested.stdout+tested.stderr if tested else built.stdout+built.stderr);raise SystemExit(0 if success else 1)
if __name__=='__main__':main()
