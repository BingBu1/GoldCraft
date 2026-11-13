# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the complete original cylinder controller branch before bone setup."""
import importlib.util,random,struct
from pathlib import Path
import unicorn
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP,UC_X86_REG_EDI,UC_X86_REG_MXCSR
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('cof_pe',ROOT/'tests/cryoffear-aim/oracle.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
ENTITY=0x22000000;STUDIO=ENTITY+0x2000;WEAPON=ENTITY+0x3000
class Replay:
 def __init__(self,sample):
  self.source=m.Original(sample);self.u=self.source.uc;self.u.mem_map(ENTITY,0x4000);self.u.mem_write(0x105457d4,m.u32(m.STUB+0x200));self.u.mem_write(m.STUB+0x200,b'\xc3');self.u.hook_add(unicorn.UC_HOOK_CODE,self.hook);self.visited=set()
 def hook(self,u,addr,size,context):
  if 0x1008d21c<=addr<0x1008d2a4:self.visited.add(addr)
  if addr==m.STUB+0x200:u.reg_write(UC_X86_REG_EAX,ENTITY if self.mode==1 else ENTITY+0x1000 if self.mode==2 else 0)
 def run(self,row):
  angle,self.mode,ident,cur,previous=row;u=self.u
  u.mem_write(STUDIO+0x30,m.u32(ENTITY));u.mem_write(WEAPON+0x9c,m.u32(ident));u.mem_write(0x1018d578,m.u32(WEAPON if ident else 0));u.mem_write(0x101b2f18,m.u32(angle));u.mem_write(ENTITY+0x318,m.u32(cur));u.mem_write(ENTITY+0xb3c,m.u32(previous))
  sp=m.STACK+0x2000;u.mem_write(sp,m.u32(m.STOP));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_EDI,STUDIO);u.reg_write(UC_X86_REG_MXCSR,0x1f80)
  u.emu_start(0x1008d21c,0x1008d2a4,count=80);assert u.reg_read(UC_X86_REG_EIP)==0x1008d2a4
  return [int(self.mode==1 and ident==21),struct.unpack('<I',u.mem_read(ENTITY+0x318,4))[0],struct.unpack('<I',u.mem_read(ENTITY+0xb3c,4))[0]]
def generate(sample,cases,expected):
 r=Replay(sample);rng=random.Random(0xc01721)
 values=[0,0x80000000,0x7f800000,0xff800000,0x7fc00001,0x7fa00001,0xffffffff]
 for angle in (-720,-360,-1,0,1,12,72,144,216,288,348,359,360,361,720,16777216):
  bits=m.bits(angle);values.extend((bits,(bits+1)&0xffffffff,(bits-1)&0xffffffff))
 values += [rng.getrandbits(32)for _ in range(512)];count=0
 for mode in (0,1,2):
  for ident in (0,3,7,19,21,0xffffffff):
   for bits in values:
    row=[bits,mode,ident,rng.getrandbits(32),rng.getrandbits(32)];result=r.run(row)
    cases.write(' '.join(f'{v:08x}'for v in row)+'\n');expected.write(' '.join(f'{v:08x}'for v in result)+'\n');count+=1
 assert all(v in r.visited for v in (0x1008d229,0x1008d233,0x1008d23c,0x1008d246,0x1008d256,0x1008d25c,0x1008d29b))
 return {'sampleSha256':m.SHA,'fragment':'1008D21C..1008D2A4','cases':count,'instructions':len(r.visited),'boundary':'Original identity/metadata gates, float instructions and six writes; renderer current entity/GetViewModel supplied; no transport/visual acceptance'}
