#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare/run real SDK BSP hull regressions, preserving every failed attempt."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('quoted',ROOT/'tools/Build-CSOLCrcBaseline.py')
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
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

def prepare(out, checkpoint):
 assert out.is_relative_to(ROOT/'build') and not out.exists()
 for p in (out,*out.parents):assert not p.is_symlink() and not p.is_junction()
 out.mkdir(parents=True)
 receipt_path=checkpoint/'receipt.json'
 inventory_path=checkpoint/'sources/source-inputs.json'
 log=checkpoint/'msbuild.log'
 receipt=read(receipt_path)
 if not (receipt['success'] and receipt['inputsUnchanged']):
  raise ValueError('A successful ReHLDS build with verified inputs is required')
 hashes={Path(row['path']):row['sha256'] for row in read(inventory_path)}
 engine=ROOT/'external/ReHLDS/rehlds/engine'
 model,world,math=(engine/name for name in ('model.cpp','world.cpp','mathlib.cpp'))
 header=ROOT/'native/engine/csol_bsp_hulls.h'
 fixture=Path(__file__).with_name('main.cpp')
 catalog=ROOT/'native/csol/scenario/monsters/monster_appearances.cpp'
 inputs=[identity(p) for p in (model,world,header,math,catalog)]
 for row in inputs:
  if hashes[Path(row['path'])]!=row['sha256']:raise ValueError('Build input changed: '+row['path'])
 inputs.extend(identity(p) for p in (fixture,Path(__file__),spec.origin,receipt_path,inventory_path,log))
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
 compile_lines=[line.strip() for line in log.read_text('utf-8-sig').splitlines()
                if 'clang-cl.exe' in line.lower() and line.rstrip().lower().endswith(r'"..\engine\model.cpp"')]
 if len(compile_lines)!=1:raise ValueError('Expected a single model.cpp compile command in msbuild.log')
 argv=helper.split_windows(compile_lines[0])
 flags=[]
 for a in argv[1:]:
  lo=a.lower()
  if lo.startswith(('/fo','/fd','/yu','/yc')) or lo.endswith(('.cpp','.cc')):continue
  if lo.startswith('/fp') and lo!='/fp:precise':continue
  flags.append(a)
 compiler=Path(argv[0]);linker=compiler.with_name('lld-link.exe')
 environment,=[row for row in receipt['controls'] if Path(row['path']).suffix.lower()=='.json']
 for required in (compiler,linker,Path(environment['path'])):
  if identity(required) not in receipt['controls']:raise ValueError('Build control changed: '+str(required))
 command=[str(compiler),'/I'+str(out),*flags,'/Y-','/showIncludes','/utf-8','/Fo'+str(out/'hulls.obj'),'/Fd'+str(out/'compile.pdb'),str(fixture)]
 link=[str(linker),'/nologo','/machine:X86','/subsystem:console','/debug','/OPT:REF','/OPT:ICF','/OPT:LLDLTO=3','/threads:1','/opt:lldltojobs=1','/out:'+str(out/'hulls.exe'),'/pdb:'+str(out/'hulls.pdb'),str(out/'hulls.obj')]
 server_maps=ROOT/'sandbox/cs-server/Half-Life/cstrike/maps'
 common=Path(os.environ.get('GOLDCRAFT_CSOL_MAPS',server_maps))
 maps=[(server_maps/name,1) for name in ('zs_deadend.bsp','zs_evacuate1.bsp','zs_evacuate2.bsp','zs_evacuate3.bsp')]
 maps += [(common/name,1) for name in ('zs_allout1.bsp','zs_allout2.bsp','zs_allout3.bsp','zs_electron2.bsp','zs_multiboss.bsp')]
 maps += [(server_maps/name,0) for name in ('cs_assault.bsp','de_dust.bsp')]
 test=[str(out/'hulls.exe')]
 for p,expected in maps:inputs.append(identity(p));test += [str(p),str(expected)]
 for p in (out/'production.inc',out/'boss_boxes.inc',compiler,linker,environment['path']):inputs.append(identity(p))
 save(out/'inputs.json',inputs)
 save(out/'plan.json',dict(inputs=identity(out/'inputs.json'),environment=environment,commands=[command,link,test],
  extraction=slices,bossBoxes=[x[0] for x in boxes],maxJobs=1,compiled=False,executed=False,expectedHeader=hashes[header],
  scope='Actual SDK layouts and extracted production loader/hull query/submodel-copy methods; real BSP planes/clipnodes. Controlled allocation/model registry, no full engine startup, moving-brush rotation or actual gameplay.'))
 print(json.dumps(dict(prepared=str(out),maps=len(maps),bossBoxes=len(boxes),header=hashes[header])))

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
   if call.returncode:raise RuntimeError(label+' failed')
   if label=='tests':
    summary=re.search(r'^[1-9]\d* checks, 0 failures$',text,re.M)
    if not summary:raise RuntimeError('Missing successful hull regression summary')
    row['summary']=summary[0]
  verify();result['success']=True
 except BaseException as e:result['error']=str(e)
 finally:
  try:verify();result['inputsUnchanged']=True
  except BaseException as e:result['inputsUnchanged']=False;result['success']=False;result['inputError']=str(e)
  result['seconds']=time.monotonic()-started;save(out/'result.json',result)
  print(json.dumps({k:v for k,v in result.items() if k!='actualIncludes'}),flush=True)
 if not result['success']:raise SystemExit(1)

if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__)
 action=parser.add_mutually_exclusive_group(required=True)
 action.add_argument('--prepare',type=Path)
 action.add_argument('--run',type=Path)
 parser.add_argument('--engine-build',type=Path,help='Successful Build-ReHLDSCheckpoint output containing a model.cpp compile command')
 args=parser.parse_args()
 if args.prepare:
  if not args.engine_build:parser.error('--prepare requires --engine-build')
  prepare(args.prepare.resolve(),args.engine_build.resolve())
 else:
  run(args.run.resolve())
