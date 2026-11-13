# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the real resource overlay and exercise its actual filesystem lifecycle."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 parser=argparse.ArgumentParser(description=__doc__)
 for name in ('commands','environment','filesystem','output'):parser.add_argument('--'+name,type=Path,required=True)
 a=parser.parse_args();out=a.output.absolute()
 if out.exists()or not out.resolve().is_relative_to(ROOT/'build'):raise ValueError('Fresh workspace build output required')
 if any(p.is_symlink()or os.path.isjunction(p)for p in (out,*out.parents)):raise ValueError('Linked output rejected')
 out.mkdir(parents=True);config=ROOT/'.tools/clang-toolchain.json';compiler=Path(json.loads(config.read_text('utf-8-sig'))['root'])/'bin/clang-cl.exe'
 db=json.loads(a.commands.read_text('utf8'));command=next(row['command']for row in db if row['file'].replace(chr(92),'/').endswith('/native/client/resource_overlay.cpp'))
 tokens=re.findall(r'"[^"\r\n]*"|[^\s]+',command)
 flags=['/nologo','/TP','/std:c++20','/EHsc','/MT','/utf-8','/W3','--target=i686-pc-windows-msvc','/O2','/clang:-O3','/clang:-flto=thin','/clang:-fuse-ld=lld','/arch:AVX2','/fp:precise','/clang:-ffp-contract=off','-Wno-overriding-option','/DNDEBUG',*[s for s in tokens if s.startswith('-D')],*['/I'+s[2:]for s in tokens if s.startswith('-I')]]
 sdk=ROOT/'external/MetaHookSv-20261007/MetaHook/include'
 sources=[ROOT/'tests/native/resource_overlay_tests.cpp',ROOT/'native/client/resource_overlay.cpp',ROOT/'native/client/resource_cache.cpp',sdk/'HLSDK/common/interface.cpp']
 inputs={*sources,Path(__file__).resolve(),a.commands.resolve(),a.environment.resolve(),a.filesystem.resolve(),config,compiler,compiler.with_name('lld-link.exe')}
 env=os.environ|json.loads(a.environment.read_text('utf-8-sig'));temp=out/'temp';temp.mkdir();env.update(TEMP=str(temp),TMP=str(temp));commands=[]
 for i,source in enumerate(sources):
  dep=out/f'{i}.d';cmd=[str(compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),str(source)];commands.append(cmd)
  run=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,timeout=60);(out/f'{i}-headers.log').write_text(run.stdout+run.stderr,'utf8');run.check_returncode()
  make=dep.read_text().replace(chr(92)+chr(10),' ')
  inputs.update(Path(v.replace(chr(92)+' ',' ')).resolve()for v in re.findall(r'(?:\\ |[^\s])+',make.split(':',1)[1]))
 before={str(p):sha(p)for p in sorted(inputs)};exe=out/'overlay.exe';cmd=[str(compiler),*flags,*map(str,sources),'/Fe:'+str(exe),'/link','/machine:x86','/threads:1','/opt:ref','bcrypt.lib'];built=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,timeout=180);(out/'compile.log').write_text(built.stdout+built.stderr,'utf8');tested=None
 if built.returncode==0:
  tested=subprocess.run([str(exe),str(a.filesystem.resolve()),str(out/'fixture')],cwd=out,capture_output=True,text=True,timeout=45);(out/'test.log').write_text(tested.stdout+tested.stderr,'utf8')
 after={str(p):sha(p)for p in sorted(inputs)};success=built.returncode==0 and tested is not None and tested.returncode==0 and before==after
 receipt={'success':success,'command':cmd,'dependencyCommands':commands,'before':before,'after':after,'unchanged':before==after,'compiled':built.returncode,'tested':tested.returncode if tested else None,'sourceCopies':0,'boundary':'Actual IFileSystem009 overlay and path admission; no installed client/provider network/gameplay/performance acceptance'}
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n','utf8');print(tested.stdout+tested.stderr if tested else built.stdout+built.stderr);raise SystemExit(0 if success else 1)
if __name__=='__main__':main()
