# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay original client10052750 with only real engine API edges controlled."""
import importlib.util,struct,hashlib,random
from pathlib import Path
import unicorn
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('cof_client_pe',ROOT/'tests/cryoffear-aim/oracle.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
ENTITY=0x22000000;EVENT=ENTITY+0x2000;API=m.DATA+0x100
class Replay:
 def __init__(self,sample):
  self.original=m.Original(sample);self.u=self.original.uc;self.u.mem_map(ENTITY,0x4000)
  for p,target in ((0x101b26dc,m.STUB+0x200),(0x101b2718,m.STUB+0x220),(0x101b2758,API),(API+4,m.STUB+0x240)):
   self.u.mem_write(p,m.u32(target))
  for addr in (m.STUB+0x200,m.STUB+0x220,m.STUB+0x240):self.u.mem_write(addr,b'\xc3')
  self.u.hook_add(unicorn.UC_HOOK_CODE,self.hook);self.visited=set()
 def hook(self,u,addr,size,context):
  if 0x10052750<=addr<0x100527b3:self.visited.add(addr)
  sp=u.reg_read(UC_X86_REG_ESP);arg=lambda n:struct.unpack('<I',u.mem_read(sp+4+n*4,4))[0]
  if addr==m.STUB+0x200:
   assert arg(0)==self.row[0];self.result[0]+=1;u.reg_write(UC_X86_REG_EAX,ENTITY if self.row[1] else 0)
  elif addr==m.STUB+0x220:
   assert (arg(0),arg(1))==(0,30);self.result[1]+=1;u.reg_write(UC_X86_REG_EAX,self.row[2])
  elif addr==m.STUB+0x240:
   sample=bytes(u.mem_read(arg(3),64)).split(b'\0')[0];assert sample==b'weapons/revolver/revolver_shell.wav'
   self.result[2]+=1;self.result[3]=arg(0);self.result[4]=arg(2)
   self.result[5:8]=struct.unpack('<3I',u.mem_read(arg(1),12));self.result[8:]=[arg(i) for i in (4,5,6,7)]
 def run(self,row):
  self.row=row;self.result=[0]*12;u=self.u
  u.mem_write(ENTITY+0xb48,struct.pack('<3I',*row[3:6]));u.mem_write(ENTITY+0xb60,struct.pack('<3I',*row[9:12]))
  u.mem_write(EVENT,bytes(72));u.mem_write(EVENT+4,m.u32(row[0]));u.mem_write(EVENT+8,struct.pack('<3I',*row[6:9]))
  sp=m.STACK+0x2000;u.mem_write(sp,m.u32(m.STOP)+m.u32(EVENT));u.reg_write(UC_X86_REG_ESP,sp)
  u.emu_start(0x10052750,m.STOP,count=200);assert u.reg_read(UC_X86_REG_EIP)==m.STOP
  return self.result

def generate(sample,cases,expected):
 r=Replay(sample);rng=random.Random(0xc0f21);count=0
 for index in (0,1,19,32,2047,0xffffffff):
  for present in (0,1):
   for pitch in (0,15,30):
    for first in (0,0x80000000,0x3f800000,0xbf800000,0x7fc00001,0x7f800000,0xff800000):
     row=[index,present,pitch,first,*[rng.randrange(0x3f000000,0x4a000000) for _ in range(8)]]
     result=r.run(row);cases.write(' '.join(f'{x:08x}' for x in row)+'\n');expected.write(' '.join(f'{x:08x}' for x in result)+'\n');count+=1
 assert all(x in r.visited for x in (0x10052769,0x10052776,0x10052788,0x100527a9,0x100527af))
 return {'sampleSha256':m.SHA,'function':'10052750..100527B3','cases':count,'instructions':len(r.visited),'scope':'Original complete event handler; entity lookup/RNG/EV_PlaySound are controlled API boundaries; no engine mixer/model/network claim'}
