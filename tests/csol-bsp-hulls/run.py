#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare/run real SDK BSP hull regressions, preserving every failed attempt."""
from pathlib import Path
import argparse
import hashlib
import ctypes
import json
import os
import re
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]
FROZEN=ROOT/'build/csol-oberon-walk-20261102/engine-candidate-01/source'
EXPECTED='ec029100408045b3ada8c348c0e00801ba84f74d5a15fec3a68446289a567485'
def split_windows(command):
    fn = ctypes.windll.shell32.CommandLineToArgvW
    fn.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    fn.restype = ctypes.POINTER(ctypes.c_wchar_p)
    count = ctypes.c_int()
    result = fn('tool ' + command, ctypes.byref(count))
    if not result:
        raise ctypes.WinError()
    try:
        return [result[i] for i in range(1, count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.LocalFree(result)
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def identity(p):
 p=Path(p).resolve()
 with p.open('rb') as f:sha=hashlib.file_digest(f,'sha256').hexdigest()
 return dict(path=str(p),bytes=p.stat().st_size,sha256=sha)
def save(p,x):
 with Path(p).open('x',encoding='utf-8') as f:json.dump(x,f,ensure_ascii=False,indent=2)
def function(path,signature):
 text=path.read_text('utf-8-sig');start=text.index(signature);opening=text.index('{',start);depth=0
 for end in range(opening,len(text)):
  depth+=(text[end]=='{')-(text[end]=='}')
  if depth==0:return text[start:end+1]+'\n'
 raise ValueError('unbalanced method')

def prepare(out):
 assert out.is_relative_to(ROOT/'build') and not out.exists()
 for p in (out,*out.parents):assert not p.is_symlink() and not p.is_junction()
 out.mkdir(parents=True)
 model=FROZEN/'external/ReHLDS/rehlds/engine/model.cpp'
 world=FROZEN/'external/ReHLDS/rehlds/engine/world.cpp'
 header=FROZEN/'native/engine/csol_bsp_hulls.h'
 assert identity(header)['sha256']==EXPECTED
 math=ROOT/'build/csol-hd-http-unified-20261031/source/external/ReHLDS/rehlds/engine/mathlib.cpp'
 fixture=Path(__file__).with_name('main.cpp')
 catalog=ROOT/'native/csol/scenario/monsters/monster_appearances.cpp'
 inputs=[identity(p) for p in (model,world,header,math,fixture,catalog,Path(__file__))]
 (out/'main.cpp').write_bytes(fixture.read_bytes());(out/'csol_bsp_hulls.h').write_bytes(header.read_bytes())
 selections=[(math,'real_t _DotProduct('),(math,'float Length('),
  (model,'float RadiusFromBounds('),(model,'void Mod_LoadPlanes('),
  (model,'void Mod_LoadClipnodes('),(model,'void Mod_LoadSubmodels('),
  (world,'hull_t *SV_HullForBsp('),(world,'int SV_HullPointContents(')]
 chunks=[];slices=[]
 for source,sig in selections:
  text=function(source,sig);chunks.append(text);slices.append(dict(origin=identity(source),signature=sig,sha256=hashlib.sha256(text.encode()).hexdigest()))
 body=function(model,'void EXT_FUNC Mod_LoadBrushModel_internal(')
 start=body.index('// regular and alternate animation')
 loop='void SetupProductionSubmodels(model_t* mod)\n{\n dmodel_t* bm;int i;\n'+body[start:]
 chunks.append(loop)
 slices.append(dict(origin=identity(model),signature='exact tail of Mod_LoadBrushModel_internal',sha256=hashlib.sha256(body[start:].encode()).hexdigest()))
 (out/'production.inc').write_text('\n'.join(chunks),'utf-8')
 boxes=[]
 for m in re.finditer(r'\{\d+,\s*"([^"]+)",\s*"[^"]+",\s*\{([^}]+)\},\s*\{([^}]+)\}',catalog.read_text('utf-8-sig')):
  lo=[float(x.strip().removesuffix('f')) for x in m[2].split(',')]
  hi=[float(x.strip().removesuffix('f')) for x in m[3].split(',')]
  if 'boss' in m[1] and hi[0]-lo[0]>36:boxes.append((m[1],m[2],m[3]))
 assert len(boxes)>=8
 boxtext='struct BossBox {const char* name;std::array<float,3> mins,maxs;};\nconstexpr BossBox BossBoxes[]={\n'
 boxtext+=''.join('{"'+n+'",{'+lo+'},{'+hi+'}},\n' for n,lo,hi in boxes)+'};\n'
 (out/'boss_boxes.inc').write_text(boxtext,'utf-8')
 meta=read(ROOT/'build/csol-oberon-walk-20261102/engine-baseline-lookup.json')
 args=split_windows(meta['originalCompileCommand'])
 flags=[]
 for a in args:
  lo=a.lower()
  if lo.startswith(('/fo','/fd','/yu','/yc')) or lo.endswith(('.cpp','.cc')):continue
  if lo.startswith('/fp') and lo!='/fp:precise':continue
  flags.append(a)
 compiler=Path(meta['compiler']['path']);linker=compiler.with_name('lld-link.exe')
 command=[str(compiler),'/I'+str(out),*flags,'/Y-','/showIncludes','/utf-8','/Fo'+str(out/'hulls.obj'),'/Fd'+str(out/'compile.pdb'),str(out/'main.cpp')]
 link=[str(linker),'/nologo','/machine:X86','/subsystem:console','/debug','/OPT:REF','/OPT:ICF','/OPT:LLDLTO=3','/threads:1','/opt:lldltojobs=1','/out:'+str(out/'hulls.exe'),'/pdb:'+str(out/'hulls.pdb'),str(out/'hulls.obj')]
 common=Path(os.environ.get('GOLDCRAFT_CSOL_MAPS', str(ROOT/'sandbox/cs-server/Half-Life/cstrike/maps')))
 maps=[(ROOT/'sandbox/cs-server/Half-Life/cstrike/maps/zs_deadend.bsp',1)]
 maps += [(common/name,1) for name in ('zs_allout1.bsp','zs_allout2.bsp','zs_allout3.bsp','zs_electron2.bsp','zs_multiboss.bsp')]
 maps += [(ROOT/'sandbox/cs-server/Half-Life/cstrike/maps'/name,0) for name in ('cs_assault.bsp','de_dust.bsp')]
 test=[str(out/'hulls.exe')]
 for p,expected in maps:inputs.append(identity(p));test += [str(p),str(expected)]
 for p in (out/'main.cpp',out/'production.inc',out/'boss_boxes.inc',out/'csol_bsp_hulls.h',compiler,linker):inputs.append(identity(p))
 save(out/'inputs.json',inputs)
 save(out/'plan.json',dict(inputs=identity(out/'inputs.json'),environment=meta['environment'],commands=[command,link,test],
  extraction=slices,bossBoxes=[x[0] for x in boxes],maxJobs=1,compiled=False,executed=False,expectedHeader=EXPECTED,
  scope='Actual SDK layouts and extracted production loader/hull query/submodel-copy methods; real BSP planes/clipnodes. Controlled allocation/model registry, no full engine startup, moving-brush rotation or actual gameplay.'))
 print(json.dumps(dict(prepared=str(out),maps=len(maps),bossBoxes=len(boxes),header=EXPECTED)))

def run(out):
 plan=read(out/'plan.json');assert plan['maxJobs']==1 and not (out/'result.json').exists()
 assert identity(plan['inputs']['path'])==plan['inputs'];inputs=read(plan['inputs']['path'])
 def verify():
  for row in inputs:assert identity(row['path'])==row,row['path']
 verify();env=os.environ.copy();env.update(read(plan['environment']['path']));temp=out/'temp';temp.mkdir()
 env.update(TMP=str(temp),TEMP=str(temp),VSLANG='1033')
 result=dict(success=False,commands=[],actualIncludes=[],scope=plan['scope']);started=time.monotonic()
 try:
  for label,command in zip(('compile','link','tests'),plan['commands']):
   before=time.monotonic();log=out/(label+'.log');print(json.dumps(dict(start=label)),flush=True)
   with log.open('xb') as stream:call=subprocess.run(command,cwd=out,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=60)
   text=log.read_text('utf-8',errors='replace')
   if label=='compile':
    for p in sorted(set(re.findall(r'^\s*Note: including file:\s*(.*?)\s*$',text,re.M)),key=str.lower):result['actualIncludes'].append(identity(p))
   row=dict(name=label,exit=call.returncode,seconds=time.monotonic()-before,log=identity(log));result['commands'].append(row)
   if label=='tests':row['summary']=re.search(r'\d+ checks, \d+ failures',text)[0]
   if call.returncode:raise RuntimeError(label+' failed')
  verify();result['success']=True
 except BaseException as e:result['error']=str(e)
 finally:
  try:verify();result['inputsUnchanged']=True
  except BaseException as e:result['inputsUnchanged']=False;result['success']=False;result['inputError']=str(e)
  result['seconds']=time.monotonic()-started;save(out/'result.json',result)
  print(json.dumps({k:v for k,v in result.items() if k!='actualIncludes'}),flush=True)
 if not result['success']:raise SystemExit(1)

if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--prepare',type=Path);parser.add_argument('--run',type=Path);args=parser.parse_args()
 if args.prepare:prepare(args.prepare.resolve())
 elif args.run:run(args.run.resolve())
 else:parser.error('choose --prepare or --run')
