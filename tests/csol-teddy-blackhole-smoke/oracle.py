#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the independent smoke component to matching original x86 instructions."""
import argparse,json,struct,subprocess,sys
from pathlib import Path
from unicorn.x86_const import *
sys.dont_write_bytecode=True
import originals as O
ROOT=Path(__file__).resolve().parents[2]
def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def state_floats(p):return [p[k] for k in ('born','die','size','initialSize','growth','alpha','mass')]+sum((list(p[k]) for k in ('origin','velocity','lastPosition','angles','rotationVelocity')),[])+[p['lastUpdate']]
def state_bits(p):return [bits(v) for v in state_floats(p)]+[p[k] for k in ('internalFlags','frame','mode')]
def describe(p):
 r=p.desc();u=p.u;at=p.shared.PARTICLE
 r['initialSize']=struct.unpack('<f',u.mem_read(at+0x128,4))[0];r['mass']=struct.unpack('<f',u.mem_read(at+0xf4,4))[0];return r
def apply(p,state):
 at=p.shared.PARTICLE;p.u.mem_write(at,p.template)
 for key,offset in (('born',0x6c),('die',0x70),('size',0x50),('initialSize',0x128),('growth',0x54),('alpha',0x64),('mass',0xf4),('lastUpdate',0x148)):p.u.mem_write(at+offset,struct.pack('<f',state[key]))
 for key,offset in (('origin',0x94),('velocity',0xb8),('lastPosition',0x13c),('angles',0xa0),('rotationVelocity',0xac)):p.u.mem_write(at+offset,struct.pack('<3f',*state[key]))
 for key,offset in (('internalFlags',0x14),('frame',0x8c)):p.u.mem_write(at+offset,struct.pack('<i',state[key]))
def step(p,state,case):
 apply(p,state);u=p.u;s=p.shared;p.fraction=case['fraction'];p.plane=case['normal'];p.trace_events=[];p.stage_order=[];before=dict(p.calls)
 u.mem_write(0x3634978,struct.pack('<f',case['previous']));u.mem_write(0x3c4d760,struct.pack('<f',case['gravity']));u.mem_write(s.CLOCK,struct.pack('<d',case['now']));u.reg_write(UC_X86_REG_ECX,s.PARTICLE);u.reg_write(UC_X86_REG_ESP,s.STACK);u.mem_write(s.STACK,struct.pack('<If',s.STOP,case['now']));u.emu_start(0x2ad3f10,s.STOP,count=20000)
 if u.reg_read(UC_X86_REG_EIP)!=s.STOP or u.reg_read(UC_X86_REG_ESP)!=s.STACK+8:raise AssertionError('Original ABI return')
 expected=describe(p);calls={k:p.calls[k]-before[k] for k in before}
 if calls!={'time':0,'camera':3,'angleBasis':1}:raise AssertionError('Original camera/AngleVectors order changed')
 if p.stage_order[:10]!=['derivedUpdate','attachedWrapper','dispatch','loopAnimation','shrink','growth','fade','rotation','motion','collision']:raise AssertionError('Original update ordering')
 trace=p.trace_events[1] if p.trace_events else None
 row=state_floats(state)+[state['internalFlags'],state['frame'],case['now'],case['previous'],case['gravity'],case['fraction'],*case['normal'],*(trace['endpos'] if trace else [0,0,0])]
 return dict(input=row,expected=dict(state=state_bits(expected),alive=int(expected['die']==0 or case['now']<expected['die']),calls=[int(trace is not None),3,1],trace=[bits(v) for v in ((trace['start']+trace['end']) if trace else [0]*6)])),expected

def run(exe,mode,rows):
 text='\n'.join(' '.join(str(v) for v in row) for row in rows)+'\n';result=subprocess.run([str(exe),mode],input=text,text=True,capture_output=True,cwd=ROOT,timeout=60)
 if result.returncode:raise AssertionError(mode+': '+result.stderr)
 values=[json.loads(line) for line in result.stdout.splitlines()]
 if len(values)!=len(rows):raise AssertionError('Output row count '+mode)
 return values

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--executable',required=True,type=Path);ap.add_argument('--client',required=True,type=Path);ap.add_argument('--evidence',required=True,type=Path);ap.add_argument('--output',required=True,type=Path);a=ap.parse_args();D=a.evidence.resolve(strict=True);exe=a.executable.resolve(strict=True);client=a.client.resolve(strict=True);out=a.output.resolve()
 if out.exists():raise FileExistsError(out)
 Ring,Smoke,inputs,ida=O.make_originals(ROOT,client,D);inputs +=[O.identity(exe),O.identity(Path(__file__)),O.identity(ROOT/'native/csol/scenario/bosses/teddy_blackhole_smoke.hpp'),O.identity(ROOT/'native/csol/scenario/bosses/teddy_spark_particle.hpp'),O.identity(ROOT/'native/csol/scenario/bosses/teddy_sparks.hpp'),O.identity(Path(__file__).with_name('client.cpp'))]
 reference=json.loads((D/'server-smoke-reference.json').read_text());packet=bytes.fromhex(reference['packet']['payload']);r=Ring();r.ring(packet.hex());base=0x3c30a88+0xb414
 emitter_expected=[]
 for i in range(36):
  at=base+i*0x168;emitter_expected.append([bits(v) for v in (*r.floats(at+0x34,3),*r.floats(at+0x4c,3),r.floats(at+0x30)[0],r.floats(at+0x124)[0],r.floats(at+0x138)[0],r.floats(at+0x28)[0])]+[r.integer(at+0x1c)])
 frames=[]
 for now in (10.,10.01,10.2,10.4,10.6,10.8,11.):
  before=len(r.particles);r.now=now;r.rng_values=[-1.,-.5,.75,1.25]*36;r.rng_index=0;r.call(0x2aa6650);frames.append([len(r.particles)-before,r.rng_index,*[r.integer(base+i*0x168+0x1c) for i in (0,35)]])
 first=r.describe(r.particles[0]['address']);last=r.describe(r.particles[-1]['address']);first['initialSize']=first['size'];last['initialSize']=last['size'];template=bytes(r.u.mem_read(r.particles[0]['address'],0x1e0));p=Smoke(template)
 ring_inputs=[[packet.hex(),*sum(([m['input'],m['output']] for m in r.math),[])]];ring_expected=[dict(emitters=emitter_expected,frames=frames,first=state_bits(first),last=state_bits(last))]
 # All coordinate extrema execute original ReadByte, case88 and signed-short reader.
 for coordinates in ((-32768,-1,32767),(0,0,0),(1,-32768,32767),(32767,32767,-32768)):
  raw=packet[:-6]+struct.pack('<3h',*coordinates);q=Ring();q.ring(raw.hex());expect=[]
  for i in range(36):
   at=base+i*0x168;expect.append([bits(v) for v in (*q.floats(at+0x34,3),*q.floats(at+0x4c,3),q.floats(at+0x30)[0],q.floats(at+0x124)[0],q.floats(at+0x138)[0],q.floats(at+0x28)[0])]+[q.integer(at+0x1c)])
  ring_inputs.append([raw.hex(),*sum(([m['input'],m['output']] for m in q.math),[])]);ring_expected.append(dict(emitters=expect))
 actual_rings=run(exe,'--ring-oracle',ring_inputs)
 for i,(expected,actual) in enumerate(zip(ring_expected,actual_rings)):
  for key,value in expected.items():
   if actual[key]!=value:raise AssertionError('ring '+str(i)+' '+key+' mismatch')
 rows=[]
 for size,growth in ((50.,2.),(60.,3.5),(70.,5.)):
  for age in (0.,.016,.499,.5,.999,1.):
   for velocity in ([0.,0.,0.],[1.,0.,0.],[49.240386962890625,8.682412147521973,0.],[30.,-20.,-25.]):
    for fraction,normal in ((1.,[0.,0.,1.]),(.5,[1.,0.,0.]),(.5,[0.,0.,1.]),(0.,[0.,0.,1.]),(.5,[0.,0.,.9]),(.5,[0.,0.,.900001])):
     state=dict(first);state.update(size=size,initialSize=size,growth=growth,origin=[198.60577392578125,-182.639892578125,300.],lastPosition=[198.60577392578125,-182.639892578125,300.],velocity=velocity);now=f32(first['born']+age);case=dict(now=now,previous=f32(now-.016),gravity=800.,fraction=fraction,normal=[f32(v) for v in normal]);row,_=step(p,state,case);rows.append(row)
 for normal in ([0.,0.,1.],[1.,0.,0.]):
  state=dict(first);state.update(size=50.,initialSize=50.,growth=2.,origin=[198.60577392578125,-182.639892578125,300.],lastPosition=[198.60577392578125,-182.639892578125,300.],velocity=[1.,0.,0.]);now=f32(first['born']+.2);case=dict(now=now,previous=f32(first['born']+.184),gravity=800.,fraction=.5,normal=normal);_,carried=step(p,state,case);case.update(now=f32(now+.016),previous=now,fraction=1.);row,_=step(p,carried,case);rows.append(row)
 actual_updates=run(exe,'--particle-oracle',[row['input'] for row in rows])
 for i,(row,actual) in enumerate(zip(rows,actual_updates)):
  if row['expected']!=actual:raise AssertionError('Update '+str(i)+' mismatch: '+json.dumps(dict(expected=row['expected'],actual=actual)))
 geometry_rows=[];geometry_expected=[];geometry_original=[]
 now=f32(first['born']+.5);_,chained=step(p,first,dict(now=now,previous=f32(now-.016),gravity=800.,fraction=1.,normal=[0,0,1]))
 for state,time in ((first,first['born']),(first,now),(first,first['die']),(chained,now)):
  apply(p,state);p.template=bytes(p.u.mem_read(p.shared.PARTICLE,0x1e0));draw=p.render(time)['draw'];commands=draw['commands'];geometry_original.append(draw)
  geometry_rows.append([*state['origin'],state['size'],state['alpha'],state['die'],time,0,-1,0,0,0,1])
  if not commands:geometry_expected.append([])
  else:
   modes=[c['values'][0] for c in commands if c['operation']=='RenderMode'];assert modes==[4,0]
   colors=next(c['values'] for c in commands if c['operation']=='Color4f');vertices=sum((c['values'] for c in commands if c['operation']=='Vertex3fv'),[]);geometry_expected.append([4,*[bits(v) for v in colors+vertices]])
 actual_geometry=run(exe,'--geometry-oracle',geometry_rows)
 if actual_geometry!=geometry_expected:raise AssertionError('Draw data mismatch')
 # Actual Add fixed128 capacity, first-free reuse, and actual allocation failure propagation.
 def add_original(q,data):
  q.u.mem_write(0x630000,data);q.u.mem_write(0x67f000,struct.pack('<II',0x500000,0x630000));q.u.reg_write(UC_X86_REG_ESP,0x67f000);q.u.reg_write(UC_X86_REG_ECX,0x3c30a88);q.u.emu_start(0x2aa49b0,0x500000,count=20000)
  assert q.u.reg_read(UC_X86_REG_EIP)==0x500000 and q.u.reg_read(UC_X86_REG_ESP)==0x67f008
 q=Ring();q.ring(packet.hex());emitter_template=q.adds[0]
 for _ in range(128-36):add_original(q,emitter_template)
 assert [q.integer(base+i*0x168+0x1c) for i in range(128)]==[5]*128
 before=bytes(q.u.mem_read(base,128*0x168));add_original(q,emitter_template);assert before==bytes(q.u.mem_read(base,128*0x168))
 q.word(base+3*0x168+0x1c,0);q.word(base+7*0x168+0x1c,0);data=bytearray(emitter_template);struct.pack_into('<3f',data,0x34,1000,2000,3000);add_original(q,bytes(data));first_free=next(i for i in range(128) if q.floats(base+i*0x168+0x34,3)==[1000,2000,3000]);capacity=[128,0,first_free,q.integer(base+7*0x168+0x1c)]
 class Failure(Ring):
  def __init__(self,n):super().__init__();self.attempt=0;self.fail=n
  def hook(self,u,address,size,context):
   if address==0x2ad2c30:self.attempt+=1
   if self.callbacks.get(address)=='allocate' and self.attempt==self.fail:self.ret(4,eax=0);return
   super().hook(u,address,size,context)
 failures=[]
 for failed in (1,20,36):
  q=Failure(failed);q.ring(packet.hex());q.now=10.01;q.rng_values=[-1.,-.5,.75,1.25]*36;q.rng_index=0;q.call(0x2aa6650)
  failures.append([failed,*[q.integer(base+i*0x168+0x1c) for i in range(36)],*[bits(q.floats(base+i*0x168+0x28)[0]) for i in range(36)],len(q.particles),q.rng_index])
 manager_expected=dict(capacity=capacity,failures=failures)
 if run(exe,'--manager-oracle',[[]])!=[manager_expected]:raise AssertionError('Manager original capacity/failure mismatch')
 unique={row['path']:row for row in inputs+p.inputs};unchanged=all(O.identity(Path(row['path']))==row for row in unique.values())
 report=dict(success=unchanged,inputsUnchanged=unchanged,inputs=list(unique.values()),completeIda=ida,ringPackets=len(ring_inputs),emitters=36*len(ring_inputs),actualCreations=len(r.particles),updates=len(rows),draws=len(geometry_rows),managerFailureCases=len(failures),managerOriginal=manager_expected,rows=rows,geometryOriginal=geometry_original,ringOriginal=ring_expected,sourceCopies=0,boundaries=dict(trigonometry='shared controlled Python math; original CRT numeric identity not claimed',random='controlled exact bounds/order',trace='controlled hull2/flags10/ignore-1; engine world trace not executed',camera='controlled27,-49,0 with camera3/AngleVectors1',render='controlled model, AngleVectors basis and TriAPI; data only'),runtimeIntegrated=False,deployed=False,liveGameplay=False,performanceAccepted=False)
 out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k in ('success','inputsUnchanged','completeIda','ringPackets','emitters','actualCreations','updates','draws')}));return 0 if unchanged else 1
if __name__=='__main__':raise SystemExit(main())
