#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare actual Meteor runtime to sealed unchanged-original instruction outputs."""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def identity(path):
 path=Path(path).resolve(strict=True)
 with path.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
 return dict(path=str(path),bytes=path.stat().st_size,sha256=digest)
def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def run(exe,mode,rows):
 p=subprocess.run([str(exe),mode],input='\n'.join(' '.join(map(str,row)) for row in rows)+'\n',
                  text=True,capture_output=True,cwd=ROOT,timeout=60)
 if p.returncode:raise AssertionError(mode+': '+p.stderr)
 values=[json.loads(line) for line in p.stdout.splitlines()]
 if len(values)!=len(rows):raise AssertionError('Missing output rows '+mode)
 return values
def spawn_fields(fields,choice):
 return [bits(v) for v in (*fields['origin'],*fields['velocity'],fields['born'],fields['die'],fields['size'],
     fields['initialSize'],fields['growth'],fields['alpha'],fields['mass'],fields['rotation'][2],fields['initialPvsTime'])]+[fields['frame'],choice]
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for name in ('executable','original','output'):ap.add_argument('--'+name,type=Path,required=True)
 a=ap.parse_args();exe=a.executable.resolve(strict=True);source=a.original.resolve(strict=True);out=a.output.resolve()
 if out.exists():raise FileExistsError(out)
 original=json.loads(source.read_text('utf8'))
 if not original['success'] or not original['inputsUnchanged'] or not original['originalOnly']:raise AssertionError('Unaccepted original receipt')
 inputs=[identity(source),identity(exe),identity(Path(__file__)),identity(ROOT/'native/csol/scenario/bosses/teddy_meteor.hpp'),
         identity(ROOT/'native/csol/scenario/core/emitter_runtime.hpp'),identity(ROOT/'native/csol/client/scenario/core/particle_effects.cpp')]
 for row in original['inputs']:
  if identity(Path(row['path']))!=row:raise AssertionError('Original input changed: '+row['path'])
  inputs.append(row)
 spawn_inputs=[];spawn_expected=[]
 for row in original['spawn']:
  fields=row['fields'];now=f32(fields['born'])
  rng=[e['output'] for e in row['events'] if e['operation']=='rng']
  if len(rng)!=8:raise AssertionError('Wrong original Float RNG count')
  spawn_inputs.append([row['packet'],row['modelChoice'],0,*map(bits,(now,now,now)),*map(bits,rng)])
  spawn_expected.append(dict(record=[2,-1,-1,1,bits(f32(now+f32(.01)))],
                             particles=[spawn_fields(fields,row['modelChoice'])],rng=8,long=1,time=3))
 clock=original['distinctClock'];events=clock['events'];times=[e['value'] for e in events if e['operation']=='time']
 rng=[e['output'] for e in events if e['operation']=='rng']
 spawn_inputs.append([original['spawn'][0]['packet'],0,0,*map(bits,times),*map(bits,rng)])
 spawn_expected.append(dict(record=[2,-1,-1,1,bits(f32(f32(times[0])+f32(.01)))],
                            particles=[spawn_fields(clock['fields'],0)],rng=8,long=1,time=3))
 fail=original['failedAllocation'];now=f32(fail[0]['value']);rng=[e['output'] for e in fail if e['operation']=='rng']
 if len(rng)!=4:raise AssertionError('Original failed factory pre-RNG count')
 spawn_inputs.append([original['spawn'][0]['packet'],1,1,*map(bits,(now,now,now)),*map(bits,rng+[-.5,.25,1.,1.125])])
 spawn_expected.append(dict(record=[2,-1,-1,1,bits(f32(now+f32(.01)))],particles=[],rng=4,long=1,time=1))
 spawn_actual=run(exe,'--meteor-runtime-oracle',spawn_inputs)
 update_inputs=[];update_expected=[]
 for row in original['updates']:
  seed=original['spawn'][row['template']]['fields'];now=row['now']
  update_inputs.append([bits(v) for v in (*seed['origin'],*seed['velocity'],seed['born'],seed['die'],seed['size'],
       seed['initialSize'],seed['growth'],seed['alpha'],seed['mass'],seed['rotation'][2],now,f32(now-.016),800.)])
  update_expected.append([bits(v) for v in (row['born'],row['die'],row['size'],row['alpha'],seed['rotation'][2],
       *row['origin'],*row['velocity'],seed['initialSize'],seed['growth'],seed['mass'])]+[row['frame']])
 update_actual=run(exe,'--meteor-update-oracle',update_inputs)
 stop_inputs=[];stop_expected=[]
 for stop in original['stops']:
  owner=stop['owner'];stop_inputs.append([owner]);matched=set(stop['matchedSlots']);rows=[]
  for i in range(128):
   group=(2,99,0)[i%3];record_owner=(0,1,7)[(i//3)%3];clear=i in matched
   if clear!=(group==2 and (owner==0 or owner==record_owner)):raise AssertionError('Original matched slot disagreement')
   rows.append([0 if clear else group,record_owner,0 if clear else -1,0 if clear else 3,0 if clear else i%3,
                bits(f32(10+i*.125)),bits(.01),1])
  stop_expected.append(rows)
 stop_actual=run(exe,'--meteor-stop-oracle',stop_inputs)
 differences=[]
 for kind,expected,actual in [('spawn',spawn_expected,spawn_actual),('update',update_expected,update_actual),('stop',stop_expected,stop_actual)]:
  for i,(x,y) in enumerate(zip(expected,actual)):
   if x!=y:differences.append(dict(kind=kind,case=i,original=x,actual=y))
 unique={i['path']:i for i in inputs};unchanged=all(identity(Path(i['path']))==i for i in unique.values())
 report=dict(success=unchanged and not differences,inputsUnchanged=unchanged,inputs=list(unique.values()),
             spawnCases=len(spawn_inputs),updateCases=len(update_inputs),stopCases=len(stop_inputs),stopRecords=len(stop_inputs)*128,
             differences=differences,sourceCopies=0,deployed=False,liveGameplay=False,performanceAccepted=False,
             boundaries='Sealed original actual parser/factory/Update/stop outputs and identities from unchanged pinned client. Production actual SDK receiver/Frame for spawn and pure Update for state math, typed common stop fields. Engine time/RNG/model/owner/allocation remain explicit boundaries; no original binary is loaded natively.')
 out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2)+'\n','utf8')
 print(json.dumps({k:report[k] for k in ('success','inputsUnchanged','spawnCases','updateCases','stopCases','stopRecords')}))
 return 0 if report['success'] else 1
if __name__=='__main__':raise SystemExit(main())
