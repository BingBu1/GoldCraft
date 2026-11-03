#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze and run the real SDK emotion event consumer using an existing client build plan."""
import argparse,hashlib,json,os,re,shutil,subprocess,time
from pathlib import Path
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def identity(p):
 p=Path(p).resolve();return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def key(p):return os.path.normcase(str(Path(p).resolve()))
def write(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--sentences',type=Path,required=True);ap.add_argument('--oracle',type=Path,required=True);a=ap.parse_args()
 root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir();source=out/'source';inputs={};production=[]
 def copy(p):
  dest=source/p.relative_to(root);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest);assert dest.stat().st_nlink==1
  inputs[key(dest)]=identity(dest);return dest
 for tree in ('native/csol','native/include'):
  for p in (root/tree).rglob('*'):
   if p.is_file() and p.suffix in ('.h','.hpp','.inl','.inc'):copy(p)
 for relative in ('native/csol/client/emotion_voice.cpp','native/csol/client/emotion_voice.hpp','native/csol/character_voice.hpp','tests/csol-character-voice-sdk/client.cpp'):
  p=root/relative;copy(p);production.append(identity(p))
 baseline=read(a.baseline);env=os.environ.copy();env.update(baseline['environment']);(out/'temp').mkdir();env.update(TEMP=str(out/'temp'),TMP=str(out/'temp'),VSLANG='1033')
 for row in baseline['inputs']:
  p=Path(row['path'])
  if p.is_file() and '/source/' not in p.as_posix():inputs[key(p)]=identity(p)
 for p in (a.baseline,a.sentences,a.oracle,Path(__file__)):inputs[key(p)]=identity(p)
 original=baseline['commands'][0]['argv'];common=[]
 for arg in original:
  if arg=='--':break
  if arg.startswith(('/Fo','/Fd')) or arg=='-c':continue
  if '/sdk-01/generated' in arg.replace('\\','/'):continue
  common.append(arg)
 commands=[]
 for name,relative in (('emotion_voice','native/csol/client/emotion_voice.cpp'),('fixture','tests/csol-character-voice-sdk/client.cpp')):
  commands.append(dict(name=name,argv=common+['/c',str(source/relative),'/Fo'+str(out/(name+'.obj')),'/Fd'+str(out/(name+'.pdb'))]))
 commands.append(dict(name='link',argv=[original[0],'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',str(out/'fixture.obj'),'/Fe'+str(out/'client.exe'),'/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1']))
 commands.append(dict(name='test',argv=[str(out/'client.exe'),str(a.sentences.resolve()),str(a.oracle.resolve())]))
 write(out/'plan.json',dict(commands=commands,inputs=list(inputs.values()),production=production,jobs=1,boundary='Actual frozen complete client TU and real MetaHook SDK; stubbed playback/resource boundary; no full DLL/game/network.'))
 result=dict(success=False,steps=[],unknown=[],changedInputs=[],changedProduction=[])
 try:
  for command in commands:
   log=out/(command['name']+'.log');started=time.monotonic()
   with log.open('xb') as stream:p=subprocess.run(command['argv'],env=env,cwd=out,stdout=stream,stderr=subprocess.STDOUT,timeout=180)
   included=[]
   for path in re.findall(r'^\s*Note: including file:\s*(.*?)\s*$',log.read_text('utf8',errors='replace'),re.M):
    if key(path) not in inputs:result['unknown'].append(path)
    else:included.append(inputs[key(path)])
   result['steps'].append(dict(name=command['name'],exit=p.returncode,seconds=time.monotonic()-started,log=identity(log),includes=included));print(command['name'],p.returncode,flush=True)
   if p.returncode or result['unknown']:break
  result['success']=len(result['steps'])==len(commands) and all(x['exit']==0 for x in result['steps']) and not result['unknown']
 except BaseException as error:result['error']=repr(error)
 finally:
  for row in inputs.values():
   if identity(row['path'])!=row:result['changedInputs'].append(row['path'])
  for row in production:
   if identity(row['path'])!=row:result['changedProduction'].append(row['path'])
  if result['changedInputs'] or result['changedProduction']:result['success']=False
  result['artifacts']=[identity(p) for p in out.glob('*.obj')]+[identity(p) for p in out.glob('*.exe')];write(out/'result.json',result)
  print(json.dumps({k:v for k,v in result.items() if k not in ('steps','artifacts')}))
 return 0 if result['success'] else 1
if __name__=='__main__':raise SystemExit(main())
