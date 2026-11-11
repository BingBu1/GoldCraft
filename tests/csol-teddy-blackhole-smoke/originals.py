# SPDX-License-Identifier: GPL-3.0-or-later
"""Controlled original x86 execution. Never loads the sample as a native DLL."""
import hashlib,importlib.util,json,math,struct,sys
from pathlib import Path
import capstone
from unicorn.x86_const import *
sys.dont_write_bytecode=True
CLIENT_SHA='f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd'
def identity(path):
 path=Path(path)
 for p in (path,*path.parents):
  if p.is_symlink() or getattr(p,'is_junction',lambda:False)():raise ValueError('Linked oracle input: '+str(p))
 path=path.resolve(strict=True)
 with path.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
 return dict(path=str(path),bytes=path.stat().st_size,sha256=digest)
def mod(n,p):
 s=importlib.util.spec_from_file_location(n,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def unwrap(value):
 while isinstance(value,dict) and ('structuredContent' in value or 'result' in value):value=value.get('structuredContent',value.get('result'))
 return value

def make_originals(ROOT,client,D):
 S=mod('blackhole_shared_originals',ROOT/'tests/csol-teddy-sliding/particle_oracle.py')
 E,P,inputs=S.make_originals(ROOT,client,ROOT/'analysis/teddy-spawn-visuals-20261111',ROOT/'analysis/teddy-spark-oracle-20261111',ROOT/'analysis/teddy-sliding-client-review-20261111')
 if identity(client)['sha256']!=CLIENT_SHA:raise ValueError('Wrong original client')
 class Ring(E):
  def __init__(self):
   super().__init__();self.adds=[];self.math=[];self.ring_mode=False
   bd=json.loads((D/'instruction-boundaries.json').read_text())
   for r in bd['items']:self.allowed[int(r['addr'],16)]=r['size']
   self.word(0x658000+0x8c,12)
  def hook(self,u,a,z,c):
   sp=u.reg_read(UC_X86_REG_ESP)
   if a==0x2812b48 and self.ring_mode:u.emu_stop();return
   if a in (0x2da898d,0x2da8891):
    x=struct.unpack('<d',u.reg_read(UC_X86_REG_XMM0).to_bytes(16,'little')[:8])[0]
    y=(math.cos if a==0x2da898d else math.sin)(x)
    self.math.append(dict(operation='cos' if a==0x2da898d else 'sin',input=x,output=y))
    u.reg_write(UC_X86_REG_XMM0,int.from_bytes(struct.pack('<d',y)+bytes(8),'little'));self.ret();return
   if a==0x2aa49b0 and self.ring_mode:self.adds.append(bytes(u.mem_read(self.integer(sp+4),0x168)))
   if self.callbacks.get(a)=='sprite_load':
    assert self.text(self.integer(sp+4))=='sprites/smoke_ia.spr';self.loads.append('sprites/smoke_ia.spr');self.ret(eax=2);return
   super().hook(u,a,z,c)
  def ring(self,payload):
   raw=bytes.fromhex(payload);assert raw[0]==88
   self.u.mem_write(0x620000,raw)
   for a,v in ((0x3ede14c,0x620000),(0x3ede150,len(raw)),(0x3ede154,0),(0x3ede158,0)):self.word(a,v)
   self.call(0x2aa4360);assert self.u.reg_read(UC_X86_REG_EAX)==88 and self.integer(0x3ede154)==1
   self.u.reg_write(UC_X86_REG_EBP,0x6f0000);self.u.reg_write(UC_X86_REG_ESP,0x6e0000)
   self.rng_values=[0.,60.,3.5]*36;self.rng_index=0;self.ring_mode=True
   self.u.emu_start(0x28126ca,0x2812b4d,count=100000);self.ring_mode=False
   assert self.u.reg_read(UC_X86_REG_EIP)==0x2812b48
   assert self.integer(0x3ede154)==len(raw) and self.integer(0x3ede158)==0
   assert len(self.adds)==36 and self.rng_index==108
  def call(self,a,args=()):
   self.u.mem_write(0x67f000,struct.pack('<I',0x500000));self.u.reg_write(UC_X86_REG_ESP,0x67f000);self.u.reg_write(UC_X86_REG_ECX,0x3c30a88)
   self.u.emu_start(a,0x500000,count=200000)
   assert self.u.reg_read(UC_X86_REG_EIP)==0x500000 and self.u.reg_read(UC_X86_REG_ESP)==0x67f004
  def describe(self,p):
   def f(o,n=1):return self.floats(p+o,n)
   def i(o):return self.integer(p+o)
   return dict(address=hex(p),vtable=hex(i(0)),drawFlags=i(8),internalFlags=i(0x14),extraFlags=i(0x150),
    size=f(0x50)[0],growth=f(0x54)[0],alpha=f(0x64)[0],fade=f(0x68)[0],born=f(0x6c)[0],die=f(0x70)[0],gravity=f(0x74)[0],
    fps=i(0x84),frames=i(0x88),frame=i(0x8c),mode=i(0x90),origin=f(0x94,3),angles=f(0xa0,3),rotationVelocity=f(0xac,3),velocity=f(0xb8,3),
    color=f(0xe8,3),mass=f(0xf4)[0],lastPosition=f(0x13c,3),lastUpdate=f(0x148)[0])

 class Smoke(P):
  def __init__(self,template):
   super().__init__(template);self.trace_events=[];self.fraction=1.;self.plane=[0.,0.,1.]
   for n in ('collision-boundaries.json','collision-helper-boundaries.json'):
    bd=json.loads((D/n).read_text())
    for rg in bd['ranges']:self.functions.append((int(rg['start'],16),int(rg['endExclusive'],16)))
   self.u.mem_write(0x3ae8f00,struct.pack('<I',0x20026000))
   self.u.mem_write(0x20026038,struct.pack('<I',0x20025000));self.u.mem_write(0x2002603c,struct.pack('<I',0x20025020))
  def ret(self):
   sp=self.u.reg_read(UC_X86_REG_ESP);dest=struct.unpack('<I',self.u.mem_read(sp,4))[0]
   self.u.reg_write(UC_X86_REG_ESP,sp+4);self.u.reg_write(UC_X86_REG_EIP,dest)
  def guard(self,u,a,z,c):
   if a==0x20025000:
    sp=u.reg_read(UC_X86_REG_ESP);h=struct.unpack('<i',u.mem_read(sp+4,4))[0];assert h==2
    self.trace_events.append(dict(operation='eventApi38',hull=h));self.ret();return
   if a==0x20025020:
    sp=u.reg_read(UC_X86_REG_ESP);start,end,flags,ignore,out=struct.unpack('<IIiiI',u.mem_read(sp+4,20))
    st=list(struct.unpack('<3f',u.mem_read(start,12)));en=list(struct.unpack('<3f',u.mem_read(end,12)))
    assert flags==10 and ignore==-1
    hit=[self.f32(x+(y-x)*self.fraction) for x,y in zip(st,en)]
    trace=bytearray(68);struct.pack_into('<f3f3ffi',trace,16,self.fraction,*hit,*self.plane,0.,0)
    u.mem_write(out,bytes(trace));self.trace_events.append(dict(operation='eventApi3C',start=st,end=en,flags=flags,ignore=ignore,fraction=self.fraction,endpos=hit,normal=self.plane));self.ret();return
   super().guard(u,a,z,c)
  def desc(self):
   p=self.shared.PARTICLE
   def f(o,n=1):return list(struct.unpack('<'+'f'*n,self.u.mem_read(p+o,4*n)))
   def i(o):return struct.unpack('<i',self.u.mem_read(p+o,4))[0]
   return dict(size=f(0x50)[0],growth=f(0x54)[0],alpha=f(0x64)[0],born=f(0x6c)[0],die=f(0x70)[0],frame=i(0x8c),mode=i(0x90),origin=f(0x94,3),angles=f(0xa0,3),rotationVelocity=f(0xac,3),velocity=f(0xb8,3),internalFlags=i(0x14),lastPosition=f(0x13c,3),lastUpdate=f(0x148)[0])
  def update(self,now,previous,fraction,velocity=None):
   s=self.shared;self.u.mem_write(s.PARTICLE,self.template);self.fraction=fraction;self.trace_events=[];self.stage_order=[]
   if velocity is not None:self.u.mem_write(s.PARTICLE+0xb8,struct.pack('<3f',*velocity))
   self.u.mem_write(0x3634978,struct.pack('<f',previous));self.u.mem_write(0x3c4d760,struct.pack('<f',800.));self.u.mem_write(s.CLOCK,struct.pack('<d',now))
   self.u.reg_write(UC_X86_REG_ECX,s.PARTICLE);self.u.reg_write(UC_X86_REG_ESP,s.STACK);self.u.mem_write(s.STACK,struct.pack('<If',s.STOP,now))
   self.u.emu_start(0x2ad3f10,s.STOP,count=20000)
   assert self.u.reg_read(UC_X86_REG_EIP)==s.STOP and self.u.reg_read(UC_X86_REG_ESP)==s.STACK+8
   assert self.stage_order[:9]==['derivedUpdate','attachedWrapper','dispatch','loopAnimation','shrink','growth','fade','rotation','motion']
   return dict(now=now,previous=previous,traceFraction=fraction,inputVelocity=velocity,stages=self.stage_order,traceEvents=self.trace_events,result=self.desc())
  def render(self,now):
   s=self.shared;self.u.mem_write(s.PARTICLE,self.template);self.u.mem_write(s.CLOCK,struct.pack('<d',now))
   self.active_draw=dict(basis=[[1.,0.,0.],[0.,-1.,0.],[0.,0.,1.]],angles=[],commands=[])
   self.u.reg_write(UC_X86_REG_ECX,s.PARTICLE);self.u.reg_write(UC_X86_REG_ESP,s.STACK);self.u.mem_write(s.STACK,struct.pack('<I',s.STOP))
   self.u.emu_start(0x1de2f10,s.STOP,count=5000)
   assert self.u.reg_read(UC_X86_REG_EIP)==s.STOP and self.u.reg_read(UC_X86_REG_ESP)==s.STACK+4
   result=self.active_draw;self.active_draw=None;return dict(now=now,draw=result)

 probe=Ring();inputs+=probe.inputs
 names=['registration-raw-ida.json','dispatch-raw-ida.json','subtype88-table-raw-ida.json','subtype88-body-raw-ida.json','readers-complete-raw-ida.json','collision-callees-raw-ida.json','collision-callback-raw-ida.json']
 for name in names:
  path=D/name;inputs.append(identity(path))
  for row in unwrap(json.loads(path.read_text('utf8'))):
   addr=int(row['addr'],16);raw=bytes(int(v,16) for v in row['data'].split())
   if bytes(probe.u.mem_read(addr,len(raw)))!=raw:raise ValueError('IDA raw mismatch '+name)
 accepted=['bounded-complete-ida-0.json','bounded-complete-ida-1.json','bounded-complete-ida-2.json','collision-complete-ida.json','collision-helpers-complete-ida.json','readers-complete-ida.json']
 decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);instruction_count=0;range_count=0
 for name in accepted:
  path=D/name;inputs.append(identity(path));rows=unwrap(json.loads(path.read_text('utf8')))
  if isinstance(rows,dict):rows=[rows]
  for row in rows:
   if row.get('truncated') or not row['cursor']['done']:raise ValueError('Incomplete IDA '+name)
   lo,hi=(int(row['query'][v],16) for v in ('start','end'))
   instructions=list(decoder.disasm(bytes(probe.u.mem_read(lo,hi-lo)),lo))
   if sum(x.size for x in instructions)!=hi-lo or [x.address for x in instructions]!=[int(x['addr'],16) for x in row['matches']]:raise ValueError('IDA instruction boundary mismatch '+name)
   instruction_count+=len(instructions);range_count+=1
 if (instruction_count,range_count)!=(841,14):raise ValueError('Unexpected full IDA scope')
 for name in ('instruction-boundaries.json','collision-boundaries.json','collision-helper-boundaries.json','server-smoke-reference.json'):inputs.append(identity(D/name))
 inputs+=[identity(Path(__file__)),identity(ROOT/'tests/csol-teddy-sliding/particle_oracle.py')]
 return Ring,Smoke,inputs,dict(instructions=instruction_count,ranges=range_count)
