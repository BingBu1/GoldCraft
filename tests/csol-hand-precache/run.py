#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the current production loader/collector directly; retain actual inputs."""
import argparse,hashlib,json,os,re,struct,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--environment',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--runtime-root',type=Path);p.add_argument('--candidate-root',type=Path)
 p.add_argument('--shotgun-root',type=Path);p.add_argument('--original-client',type=Path);p.add_argument('--shotgun-proof',type=Path);a=p.parse_args()
 if bool(a.runtime_root)!=bool(a.candidate_root):raise ValueError('Both real resource roots are required')
 if any((a.shotgun_root,a.original_client,a.shotgun_proof)) and not all((a.shotgun_root,a.original_client,a.shotgun_proof)):raise ValueError('Shotgun resources, original client and pre-edit proof must be supplied together')
 if a.shotgun_root and a.runtime_root:raise ValueError('Run real hand and real shotgun resource modes separately')
 if a.runtime_root and (not a.runtime_root.resolve().is_relative_to(ROOT/'sandbox') or not a.candidate_root.resolve().is_relative_to(ROOT/'build')):raise ValueError('Use workspace resource roots')
 proof_check=None
 if a.shotgun_root:
  if not a.shotgun_root.resolve().is_relative_to(ROOT/'build') or not a.original_client.resolve().is_relative_to(ROOT/'analysis') or not a.shotgun_proof.resolve().is_relative_to(ROOT/'analysis'):raise ValueError('Use workspace authored resources and private evidence copies')
  proof=json.loads(a.shotgun_proof.read_text('utf8'));model=a.shotgun_root/'models/guest/cof/shotgun/v_shotgun.mdl'
  if not proof['beforeImplementation'] or sha(a.original_client)!=proof['clientSha256'] or sha(model)!=proof['model']['sha256']:raise ValueError('Original sample/model identity changed')
  parent=ROOT/Path(proof['parent']['path'].replace(chr(92),'/'))
  if sha(parent)!=proof['parent']['sha256']:raise ValueError('Parent original consumer evidence changed')
  raw=a.original_client.read_bytes();pe=struct.unpack_from('<I',raw,0x3c)[0];count=struct.unpack_from('<H',raw,pe+6)[0];optional=pe+24
  if raw[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',raw,pe+4)[0]!=0x14c or struct.unpack_from('<H',raw,optional)[0]!=0x10b:raise ValueError('Expected original PE32/x86 client')
  base=struct.unpack_from('<I',raw,optional+28)[0];section=optional+struct.unpack_from('<H',raw,pe+20)[0]
  for region in proof['clientRegions']:
   rva=int(region['va'],0)-base;data=None
   for i in range(count):
    at=section+i*40;size,va,length,offset=struct.unpack_from('<IIII',raw,at+8)
    if va<=rva and rva+region['size']<=va+length:data=raw[offset+rva-va:offset+rva-va+region['size']];break
   if data is None or hashlib.sha256(data).hexdigest()!=region['sha256'] or not region['idaEqualsPE']:raise ValueError('Original IDA/PE consumer region changed')
  for missing in ('sound/weapons/syringe/syringe_sleeve.wav','sound/guest/cof/weapons_new/syringe/syringe_sleeve.wav'):
   if (a.shotgun_root/missing).exists():raise ValueError('Unexpected substitute for originally missing draw cue')
  proof_check=dict(clientSha256=sha(a.original_client),modelSha256=sha(model),regions=len(proof['clientRegions']),bytes=sum(r['size'] for r in proof['clientRegions']),preEdit=True)
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
 if a.shotgun_root:
  inputs.update(p.resolve() for p in a.shotgun_root.rglob('*') if p.is_file())
  inputs.update({a.original_client.resolve(),a.shotgun_proof.resolve(),parent.resolve()})
 if a.runtime_root:
  for relative in ('models/guest/cof','sound/guest/cof','events/guest/cof','aurora/guest/cof','gfx/guest/cof','gfx/tattoo','sprites/guest/cof','inventoryitems/guest/cof'):
   inputs.update(p.resolve() for p in (a.runtime_root/relative).rglob('*') if p.is_file())
  inputs.update(p.resolve() for p in (a.candidate_root/'models').rglob('*') if p.is_file())
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
  arguments=[str(exe)]+(['--shotgun-root',str(a.shotgun_root.resolve())] if a.shotgun_root else [str(a.runtime_root.resolve()),str(a.candidate_root.resolve())] if a.runtime_root else [])
  proc=subprocess.run(arguments,cwd=out,capture_output=True,text=True,timeout=30);(out/'test.log').write_text(proc.stdout+proc.stderr,'utf8');print(proc.stdout+proc.stderr);proc.check_returncode()
 except Exception as e:error=str(e)
 after={str(x):sha(x) for x in sorted(inputs)}
 receipt=dict(success=error is None and before==after,error=error,commands=commands,before=before,after=after,inputsUnchanged=before==after,originalProof=proof_check,realShotgunModel=bool(a.shotgun_root),sourceCopies=0,gameplay=False,performance=False,boundary='Current complete loader/collector/cache with actual ReHLDS SDK; VFS and model registry doubled; no full engine/gameplay')
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf8')
 raise SystemExit(0 if receipt['success'] else 1)
if __name__=='__main__':main()
