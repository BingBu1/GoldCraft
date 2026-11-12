#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute original constructor/Add/manager/retirement on retained multi-system chains."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
from unicorn.x86_const import UC_X86_REG_EAX
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("aurora_oracle",ROOT/"tests/cryoffear-aurora/oracle.py")
old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
base=old.base
INPUT=struct.Struct("<iifffii")
HEADER=struct.Struct("<3I")
RECORD=struct.Struct("<11I")
class Original(old.Original):
 def __init__(self,sample,script):
  self.multi=False;self.viewcalls=0
  super().__init__(sample,script)
  self.uc.mem_map(old.probe.HEAP+0x10000,0x400000)
  self.next=old.probe.HEAP+0x10000;self.multi=True
  self.uc.mem_write(0x105448C8,base.u32(0))
  self.uc.mem_write(0x105433C8,base.u32(base.DATA+0x500))
  self.uc.mem_write(0x101B2744,base.u32(base.STUB+0x500))
  self.uc.mem_write(0x101B274C,base.u32(base.STUB+0x520))
  self.uc.mem_write(base.STUB+0x500,b"\xC3");self.uc.mem_write(base.STUB+0x520,b"\xC3")
  self.uc.mem_write(base.CVARS+0xF00,b"aurora/gunsmoke.aur\0")
  self.randoms=0;self.viewcalls=0
 def allocate(self,size):
  if not self.multi:return super().allocate(size)
  addr=self.next;self.next=(addr+size+15)&~15
  assert self.next<old.probe.HEAP+0x410000
  self.uc.mem_write(addr,bytes(size));self.allocations.append((addr,size));return addr
 def service(self,uc,address,size,data):
  if address==base.STUB+0x500:self.ret(old.probe.SCRIPT)
  elif address==base.STUB+0x520:self.ret()
  elif address==base.STUB+0x320:
   self.viewcalls+=1;self.ret(old.VIEW if self.hasview else 0)
  elif address==base.STUB+0x280 and self.multi:
   low,high=struct.unpack("<2f",uc.mem_read(uc.reg_read(old.UC_X86_REG_ESP)+4,8))
   # Match the fixture's float midpoint service, outside original instructions.
   value=base.f32(low+base.f32(base.f32(high-low)*.5));self.randoms+=1
   uc.mem_write(base.DATA+0x180,struct.pack("<f",value))
   uc.mem_write(base.STUB+0x400,b"\xD9\x05"+base.u32(base.DATA+0x180)+b"\xC3")
   uc.reg_write(old.UC_X86_REG_EIP,base.STUB+0x400)
  else:super().service(uc,address,size,data)
 def call(self,address,args=(),this=0):
  # Large retained chains may exceed the single-system probe instruction bound.
  uc=self.uc;sp=base.STACK+0x2000
  uc.mem_write(sp,base.u32(base.STOP)+b"".join(base.u32(x) for x in args))
  uc.reg_write(old.UC_X86_REG_ESP,sp);uc.reg_write(old.probe.UC_X86_REG_ECX,this)
  uc.emu_start(address,base.STOP,count=2000000)
  assert uc.reg_read(old.UC_X86_REG_EIP)==base.STOP
  return uc.reg_read(UC_X86_REG_EAX)
 def snapshot(self):
  uc=self.uc;records=[];p=struct.unpack("<I",uc.mem_read(0x105448C8,4))[0]
  while p:
   assert len(records)<2048
   owner=struct.unpack("<I",uc.mem_read(p+0xC,4))[0]
   state=struct.unpack("<I",uc.mem_read(p+0x98,4))[0]
   kill=struct.unpack("<I",uc.mem_read(p+0xA8,4))[0]
   pool=struct.unpack("<I",uc.mem_read(p+0xB4,4))[0]
   head,free,main=[struct.unpack("<I",uc.mem_read(p+o,4))[0] for o in (4,0xB8,0xBC)]
   slot=lambda x:0xFFFFFFFF if not x else (x-pool)//136
   age,life=struct.unpack("<2I",uc.mem_read(main+124,8)) if main else (0,0)
   fade=struct.unpack("<3I",uc.mem_read(p+0x9C,12))
   records.append(RECORD.pack(owner,state,kill,slot(head),slot(free),slot(main),age,life,*fade))
   p=struct.unpack("<I",uc.mem_read(p,4))[0]
  return HEADER.pack(len(records),self.randoms,self.viewcalls)+b"".join(records)
 def event(self,row):
  op,owner,dt,time,limit,body,mode=row;uc=self.uc
  uc.mem_write(base.DATA,struct.pack("<f",time));uc.mem_write(old.OWNER+0x304,base.u32(body))
  uc.mem_write(base.DATA+0x50C,struct.pack("<f",limit))
  if op==1:
   p=self.allocate(200)
   self.call(0x10081080,(owner,base.CVARS+0xF00,1,0,old.MODEL),p)
   self.call(0x10080AF0,(p,),0x105448C8)
  if op==2:self.call(0x10080C90,(base.bits(dt),mode),0x105448C8)
  if op in (3,4):self.call(0x10080B90,(1 if op==3 else 0,),0x105448C8)
  if op==5:self.call(0x10080B40,(owner,),0x105448C8)
  if op==6:self.call(0x10080B00,(),0x105448C8)
  if op==7:self.hasview=False
  if op==8:self.hasview=True
  return self.snapshot()
def events():
 time=0.
 # Repeated original constructor/pool/Add and natural completion retain192 nodes.
 for shot in range(192):
  owner=5 if shot%2 else 0
  yield (1,owner,0.,time,1000000.,0,0)
  yield (2,0,0.,time,1000000.,0,0)
  time+=2.;yield (2,0,2.,time,1000000.,0,0)
  time+=4.;yield (2,0,4.,time,1000000.,0,0)
 yield (5,0,0.,time,0.,0,0);yield (5,5,0.,time,0.,0,0)
 yield (3,0,0.,time,0.,0,0)
 for body in (70,184,137):yield (2,0,0.,time,0.,body,0)
 yield (2,0,0.,time,0.,0,1)
 yield (1,5,0.,time,-1.,0,0)
 yield (7,0,0.,time,0.,0,0);yield (2,0,0.,time,0.,0,0)
 yield (8,0,0.,time,0.,0,0);yield (6,0,0.,time,0.,0,0)
 # Soft drain retains main age<life contribution, affecting future ctor limit.
 yield (1,5,0.,time,1000000.,0,0);yield (2,0,0.,time,0.,0,0)
 yield (4,0,0.,time,0.,0,0);yield (2,0,0.,time,0.,0,0)
 time+=4.;yield (2,0,.01,time,0.,0,0)
 yield (1,0,0.,time,0.,0,0);yield (2,0,0.,time,0.,70,0)
 yield (2,0,0.,time,0.,0,0);yield (5,5,0.,time,0.,0,0)
 yield (6,0,0.,time,0.,0,0)
def main():
 p=argparse.ArgumentParser();p.add_argument('--sample',type=Path,required=True)
 p.add_argument('--runner',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 script=a.sample.parent.parent/'aurora-gunsmoke-original.txt';original=Original(a.sample,script.read_bytes())
 payload=b''.join(INPUT.pack(*r) for r in events());rows=list(INPUT.iter_unpack(payload))
 expected=b''.join(original.event(r) for r in rows)
 result=subprocess.run([str(a.runner.resolve()),str(script.resolve()),'--oracle'],input=payload,capture_output=True)
 if result.returncode:raise RuntimeError(result.stderr.decode(errors='replace'))
 actual=result.stdout
 offset=0;mismatches=[];maxnodes=0
 for event,row in enumerate(rows):
  n,_,_=HEADER.unpack_from(expected,offset);maxnodes=max(maxnodes,n);size=HEADER.size+n*RECORD.size
  if actual[offset:offset+size]!=expected[offset:offset+size]:
   diffs=[i for i in range(0,size,4) if actual[offset+i:offset+i+4]!=expected[offset+i:offset+i+4]]
   mismatches.append(dict(event=event,input=row,byteOffsets=diffs[:20],actualWords=[struct.unpack_from("<I",actual,offset+i)[0] for i in diffs[:20] if offset+i+4<=len(actual)],expectedWords=[struct.unpack_from("<I",expected,offset+i)[0] for i in diffs[:20]]))
   break
  offset+=size
 receipt=dict(sampleSha256=base.SHA,events=len(rows),maxRetainedNodes=maxnodes,bytes=len(expected),
  inputSha256=hashlib.sha256(payload).hexdigest(),originalSha256=hashlib.sha256(expected).hexdigest(),
  nativeSha256=hashlib.sha256(actual).hexdigest(),mismatches=mismatches,
  fields='every logical node owner/state/kill/head/free/main/main-age/main-life/deadline/fade bits, RNG and GetViewModel counts',
  originalExecution='actual constructor parser/pool, Add, Frame/Update, DeleteOwner, KillLocal and Clear instructions',
  modeledServices=['zero allocation (authorized initialization)','script tokenizer/resource','CRT leaf helpers','engine callbacks and triangle service'],
  live=False,performance=False)
 a.output.write_text(json.dumps(receipt,indent=2),encoding='utf8')
 assert len(actual)==len(expected),(len(actual),len(expected))
 assert not mismatches,mismatches
 print(f'Aurora retained chain: {len(rows)} original events, max{maxnodes} nodes, zero raw-state/RNG/view-callback mismatches')
if __name__=='__main__':main()
