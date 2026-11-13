#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the current production loader/collector directly; retain actual inputs."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--environment',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 out=a.output.absolute()
 if out.exists() or not out.resolve().is_relative_to(ROOT/'build'):raise ValueError('Fresh workspace output required')
 out.mkdir(parents=True);(out/'temp').mkdir()
 # Only exact production function bodies form this generated SDK adapter.
 native=ROOT/'external/ReHLDS/rehlds/engine/sv_main.cpp';native_text=native.read_text('utf-8')
 def body(signature):
  start=native_text.index(signature);opening=native_text.index('{',start);depth=1;end=opening+1
  while depth:
   if native_text[end]=='{':depth+=1
   elif native_text[end]=='}':depth-=1
   end+=1
  return native_text[start:end]+'\n'
 (out/'native_studio_sounds.inc').write_text(body('static void PrecacheModelSoundsForFile(')+body('void PrecacheModelSounds('),'utf8')
 env=os.environ|json.loads(a.environment.read_text('utf-8-sig'));env.update(TEMP=str(out/'temp'),TMP=str(out/'temp'))
 compiler=Path(json.loads((ROOT/'.tools/clang-toolchain.json').read_text('utf-8-sig'))['root'])/'bin/clang-cl.exe'
 include=['native/engine','native/include','external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include']
 include+=['external/ReHLDS/rehlds/'+x for x in ('','metamod/include','public/rehlds','common','engine','public','pm_shared','rehlds','testsuite')]
 include+=['external/ReHLDS/dep/bzip2/include','external/ReHLDS/dep/cppunitlite/include']
 flags=['/nologo','/TP','/std:c++20','/EHsc','/MT','/Z7','/utf-8','/Gy','/Gw','/arch:AVX2','/fp:precise','/clang:-O3','/clang:-flto=thin','/clang:-ffp-contract=off','--target=i686-pc-windows-msvc','/D_CRT_SECURE_NO_WARNINGS','/DNOMINMAX']
 flags+=['/D'+x for x in ('REHLDS_API','REHLDS_FIXES','REHLDS_SSE','REHLDS_OPT_PEDANTIC','REHLDS_SELF','REHLDS_CHECKS','HAVE_OPT_STRTOOLS','USE_BREAKPAD_HANDLER','DEDICATED','SWDS','_MBCS')]
 flags+=['/imsvc'+str(ROOT/x) for x in include]+['/I'+str(out)]
 sources=[ROOT/'tests/csol-hand-precache/main.cpp']+[ROOT/('native/csol/'+n+'.cpp') for n in ('dependencies','model_codec','sprite_codec','texture_model','model_json','events','model_source','sequences')]
 inputs=set(sources)|{Path(__file__).resolve(),a.environment.resolve(),compiler,compiler.with_name('lld-link.exe'),ROOT/'external/ReHLDS/rehlds/lib/libacof32.lib',native,out/'native_studio_sounds.inc'}
 for i,src in enumerate(sources):
  dep=out/(str(i)+'.d');cmd=[str(compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),str(src)]
  proc=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True,timeout=90);(out/(str(i)+'-headers.log')).write_text(proc.stdout+proc.stderr,'utf8');proc.check_returncode()
  text=dep.read_text().replace(chr(92)+chr(10),' ')
  inputs.update(Path(n.replace(chr(92)+' ',' ')).resolve() for n in re.findall(r'(?:\\ |[^\s])+',text.split(':',1)[1]))
 before={str(x):sha(x) for x in sorted(inputs)}
 objects=[];commands=[];error=None
 try:
  for i,src in enumerate(sources):
   obj=out/(str(i)+'.obj');cmd=[str(compiler),*flags,'/c',str(src),'/Fo'+str(obj)];commands.append(cmd)
   proc=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,timeout=150);(out/(str(i)+'-compile.log')).write_text(proc.stdout+proc.stderr,'utf8')
   if proc.returncode:print(proc.stdout+proc.stderr)
   proc.check_returncode();objects.append(str(obj))
  exe=out/'hand-precache.exe';cmd=[str(compiler.with_name('lld-link.exe')),'/nologo','/machine:x86','/debug','/opt:ref','/opt:icf','/out:'+str(exe),*objects,str(ROOT/'external/ReHLDS/rehlds/lib/libacof32.lib'),'bcrypt.lib'];commands.append(cmd)
  proc=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,timeout=150);(out/'link.log').write_text(proc.stdout+proc.stderr,'utf8')
  if proc.returncode:print(proc.stdout+proc.stderr)
  proc.check_returncode()
  proc=subprocess.run([str(exe)],cwd=out,capture_output=True,text=True,timeout=30);(out/'test.log').write_text(proc.stdout+proc.stderr,'utf8');print(proc.stdout+proc.stderr);proc.check_returncode()
 except Exception as e:error=str(e)
 after={str(x):sha(x) for x in sorted(inputs)}
 receipt=dict(success=error is None and before==after,error=error,commands=commands,before=before,after=after,inputsUnchanged=before==after,sourceCopies=0,gameplay=False,performance=False,boundary='Current complete loader/collector/cache with actual ReHLDS SDK; VFS and model registry doubled; no full engine/gameplay')
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf8')
 raise SystemExit(0 if receipt['success'] else 1)
if __name__=='__main__':main()
