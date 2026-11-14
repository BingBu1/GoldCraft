#!/usr/bin/env python3
"""Compare P345 emission and32x124 light pool against pinned original x86.
No proprietary module, resource or instruction byte is distributed. Host API,
model lookup, RNG, clock and bone-transformed attachment are supplied fixtures.
"""
import argparse,hashlib,importlib.util,json,random,struct,subprocess
from pathlib import Path
import unicorn
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP,UC_X86_REG_ECX
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('base',ROOT/'tests/cryoffear-aim/oracle.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
POOL=0x104CC550;LAST=0x104CD4E8;X=0x22000000;LOCAL=X;VIEW=X+0x1000;MODEL=X+0x2000;WEAPON=X+0x3000;API=X+0x4000;EVENTAPI=X+0x5000;DL=X+0x6000;STUDIO=X+0x7000;HEADER=X+0xC000;WORLD=X+0xD000
HEADER_IN=struct.Struct('<I5fI'); OUTPUT_SIZE=8+3000+40+3972+36+12+4+64
class Replay:
 def __init__(self,sample,weapon='P345'):
  self.o=m.Original(sample);self.u=u=self.o.uc;u.mem_map(X,0x20000)
  for a,v in [(0x101B2754,API),(API+0x100,m.STUB+0x200),(0x101B26D4,m.STUB+0x220),(0x101B26F8,m.STUB+0x240),(0x101B2714,m.STUB+0x260),(0x101B26FC,m.STUB+0x280),(0x101B2758,EVENTAPI),(EVENTAPI+12,m.STUB+0x2A0),(API+0xC8,m.STUB+0x2C0),(0x101B2668,m.STUB+0x2E0),(0x1018D578,WEAPON),(0x105433CC,m.CVARS+0x100),(STUDIO+0x44,HEADER),(STUDIO+0x40AC,X+0xE000),(HEADER+0xD4,1),(HEADER+0xD8,0x100),(WEAPON+156,7)]:u.mem_write(a,m.u32(v))
  u.mem_write(HEADER+0x124,m.u32(0));u.mem_write(MODEL,b'models/weapons/p345/v_p345.mdl\0');u.mem_write(WORLD,b'police/models/weapons/p345/p_p345.mdl\0')
  u.mem_write(API+0xC0,m.u32(m.STUB+0x300))
  for a in [m.STUB+0x200,m.STUB+0x220,m.STUB+0x240,m.STUB+0x280,m.STUB+0x2A0,m.STUB+0x2C0,m.STUB+0x2E0,m.STUB+0x300]:u.mem_write(a,b'\xC3')
  u.mem_write(m.STUB+0x260,b'\xD9\x05'+m.u32(m.DATA+16)+b'\xC3');u.hook_add(unicorn.UC_HOOK_CODE,self.hook)
  self.ranges={hex(a):hashlib.sha256(u.mem_read(a,n)).hexdigest() for a,n in [(0x1004BC30,1166),(0x10090130,1209),(0x1004BB80,169),(0x1005C0F0,137),(0x1005C180,128),(0x1005BFF0,79),(0x1005C3D0,21)]}
  self.default_weapon=21 if weapon=='Revolver' else 7
  if weapon=='Revolver':
   u.mem_write(MODEL,b'models/weapons/revolver/v_revolver.mdl\0')
   u.mem_write(WORLD,b'police/models/weapons/revolver/p_revolver.mdl\0')
 def string(self,a):
  b=bytearray()
  while self.u.mem_read(a,1)!=b'\0':b+=self.u.mem_read(a,1);a+=1
  return b
 def hook(self,u,a,n,ctx):
  sp=u.reg_read(UC_X86_REG_ESP)
  if a in (0x100D2C80,0x100D3CF0,0x10091790):
   ret,p,q,z=struct.unpack('<4I',u.mem_read(sp,16))
   if a==0x100D2C80:u.mem_write(p,bytes([q&255])*z);value=p
   elif a==0x100D3CF0:
    at=self.string(p).find(self.string(q));value=0 if at<0 else p+at
   else:
    index=(p-(HEADER+0x128))//0x58
    assert index in (0,1),hex(p)
    u.mem_write(z,self.attachments[index]);value=0
   u.reg_write(UC_X86_REG_EAX,value);u.reg_write(UC_X86_REG_ESP,sp+4);u.reg_write(UC_X86_REG_EIP,ret)
  elif a==m.STUB:
   self.calls.append(1);u.mem_write(m.DATA,struct.pack('<f',self.now if self.timeIndex==0 else m.f32(self.now+m.f32(self.timeIndex*0.00001))));self.timeIndex+=1
  elif a==m.STUB+0x200:self.calls.append(8);u.reg_write(UC_X86_REG_EAX,DL)
  elif a==m.STUB+0x220:self.calls.append(2);u.reg_write(UC_X86_REG_EAX,LOCAL)
  elif a==m.STUB+0x240:
   self.calls.append(3);idx=struct.unpack('<I',u.mem_read(sp+8,4))[0];u.mem_write(idx,m.u32(13));u.reg_write(UC_X86_REG_EAX,MODEL)
  elif a==m.STUB+0x260:
   self.calls.append(4);u.mem_write(m.DATA+16,struct.pack('<f',self.roll if self.randomIndex else self.size));self.randomIndex+=1
  elif a==m.STUB+0x280:
   self.calls.append(5);p=struct.unpack('<I',u.mem_read(sp+8,4))[0];self.visible=bytearray(u.mem_read(p,3000));self.visible[2964:2968]=bytes(4)
  elif a==m.STUB+0x2A0:self.calls.append(6);u.reg_write(UC_X86_REG_EAX,27)
  elif a==m.STUB+0x2C0:
   self.calls.append(7);args=list(struct.unpack('<9I',u.mem_read(sp+4,36)));self.direction=bytes(u.mem_read(args[1],12));args[:2]=[1,1];self.sprite=struct.pack('<9I',*args);u.reg_write(UC_X86_REG_EAX,0)
  elif a==m.STUB+0x2E0:self.calls.append(9)
  elif a==m.STUB+0x300:
   self.calls.append(10);origin,velocity,angles,life,index,sound=struct.unpack('<6I',u.mem_read(sp+4,24))
   self.sprite=bytes(u.mem_read(origin,12))+bytes(u.mem_read(velocity,12))+struct.pack('<3I',life,index,sound)
   self.direction=bytes(u.mem_read(angles,12));u.reg_write(UC_X86_REG_EAX,0)
 def run(self,row):
  header,local,view,pool=row;op,self.now,self.size,self.roll,scale,cheap,att=HEADER_IN.unpack(header);u=self.u
  self.calls=[];self.timeIndex=self.randomIndex=0;self.visible=bytes(3000);self.sprite=bytes(36);self.direction=bytes(12);self.attachments=[view[2912:2924],view[2924:2936]]
  u.mem_write(LOCAL,local);u.mem_write(VIEW,view);u.mem_write(VIEW+2964,m.u32(MODEL));u.mem_write(POOL,pool[:3968]);u.mem_write(LAST,pool[3968:]);u.mem_write(DL,bytes([0xCD])*40);u.mem_write(m.CVARS+0x10C,struct.pack('<f',cheap));u.mem_write(0x101BA478,m.u32(7))
  u.mem_write(HEADER+0xD4,m.u32(2));u.mem_write(WEAPON+156,m.u32(3 if op in (7,8) else self.default_weapon))
  u.mem_write(0x101BA478,m.u32(3 if op in (7,8) else self.default_weapon));u.mem_write(0x101BA480,view[2900:2912])
  args=[];addr=0;selected=-1;result=0
  if op==6:return struct.pack('<Ii',4,-1)+bytes(3000)+bytes([0xCD])*40+pool+bytes(36+12+4+64)
  if op in (0,8):addr=0x1004BC30;args=[VIEW,att,0,m.bits(scale)]
  elif op in (1,7):addr=0x10090130;args=[WORLD,0,0,m.bits(scale)];u.reg_write(UC_X86_REG_ECX,STUDIO)
  elif op==2:addr=0x1005C0F0;args=[att]
  elif op==3:addr=0x1005C180
  elif op==4:addr=0x1005BFF0
  elif op==5:addr=0x1005C3D0
  u.mem_write(m.DATA,struct.pack('<f',self.now));sp=m.STACK+0x2000;u.mem_write(sp,m.u32(m.STOP)+b''.join(m.u32(a) for a in args));u.reg_write(UC_X86_REG_ESP,sp);u.emu_start(addr,m.STOP,count=30000)
  if op==2:selected=(u.reg_read(UC_X86_REG_EAX)-POOL)//124
  if op==4:selected=u.reg_read(UC_X86_REG_EAX)
  if op in (2,3,4):self.calls=[] # Pool helpers take explicit clock in production.
  return struct.pack('<Ii',result,selected)+bytes(self.visible)+bytes(u.mem_read(DL,40))+bytes(u.mem_read(POOL,3968))+bytes(u.mem_read(LAST,4))+self.sprite+self.direction+struct.pack('<I16I',len(self.calls),*(self.calls+[0]*(16-len(self.calls))))
def cases():
 rng=random.Random(345)
 for op in range(9):
  for index in range(1 if op==6 else 180 if op in (7,8) else 120):
   now=m.f32(rng.choice([0,1,2,100,16777216]) if index<10 else rng.uniform(-2,100));size=m.f32(rng.uniform(-1,1));roll=m.f32(rng.uniform(1,359));scale=m.f32(rng.choice([0,1,0.3,10]));cheap=rng.choice([0,1]);att=rng.randrange(4) if op==0 else rng.choice([0,0,17]) if op==2 else 0
   local=bytearray(rng.randbytes(3000));view=bytearray(rng.randbytes(3000));struct.pack_into('<3f',local,2900,10,20,30);struct.pack_into('<3f',view,2900,-10,5,20)
   for a in range(4):struct.pack_into('<3f',view,2912+12*a,a+1,a+2,a+3)
   pool=bytearray(rng.randbytes(3972));last=m.f32(now+rng.choice([-10,-1,-.1,0,.1,1,10]));struct.pack_into('<f',pool,3968,last)
   for i in range(32):
    radius=m.f32(rng.choice([0,-0.0,128,rng.uniform(-10,300)]));die=m.f32(now+rng.choice([-1,0,1]));decay=m.f32(rng.uniform(-10,1000));key=rng.choice([0,0,17]);struct.pack_into('<f',pool,i*124+12,radius);struct.pack_into('<ffi',pool,i*124+28,die,decay,key)
   yield HEADER_IN.pack(op,now,size,roll,scale,cheap,att),bytes(local),bytes(view),bytes(pool)
def main():
 p=argparse.ArgumentParser();p.add_argument('--sample',type=Path,required=True);p.add_argument('--runner',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--weapon',choices=['P345','Revolver'],default='P345');a=p.parse_args();r=Replay(a.sample,a.weapon);rows=list(cases());named=[]
 for name,dies,keys,key,slot in [('equal-deadline',[1]*32,[0]*32,0,0),('strict-expired-slot1',[1,0]+[1]*30,[0]*32,0,1),('full-slot0',[2]*32,[0]*32,0,0),('matching-key-before-expiry',[0]*32,[0]*7+[17]+[0]*24,17,7)]:
  h,l,v,pool=rows[240];pool=bytearray(pool)
  for i in range(32):struct.pack_into('<f',pool,i*124+28,dies[i]);struct.pack_into('<i',pool,i*124+36,keys[i])
  row=(HEADER_IN.pack(2,1,0,1,1,0,key),l,v,bytes(pool));named.append((name,len(rows),slot));rows.append(row)
 for op in (2,3,4):
  for field in ('now','last','radius','die','decay'):
   for value in (float('nan'),float('inf'),-float('inf'),-0.0):
    h,l,v,pool=rows[op*120];values=list(HEADER_IN.unpack(h));pool=bytearray(pool)
    if field=='now':values[1]=value
    elif field=='last':struct.pack_into('<f',pool,3968,value)
    else:
     for i in range(32):struct.pack_into('<f',pool,i*124+dict(radius=12,die=28,decay=32)[field],value)
    rows.append((HEADER_IN.pack(*values),l,v,bytes(pool)))
 payload=b''.join(b''.join(row) for row in rows);expected=b''.join(r.run(row) for row in rows);actual=subprocess.run([str(a.runner.resolve()),*(['--revolver']if a.weapon=='Revolver'else[])],input=payload,capture_output=True,check=True).stdout;assert len(actual)==len(expected)==len(rows)*OUTPUT_SIZE,(len(actual),len(expected),OUTPUT_SIZE)
 for name,index,slot in named:assert struct.unpack_from('<i',expected,index*OUTPUT_SIZE+4)[0]==slot,name
 failures=[]
 for i in range(len(rows)):
  want=expected[i*OUTPUT_SIZE:(i+1)*OUTPUT_SIZE];got=actual[i*OUTPUT_SIZE:(i+1)*OUTPUT_SIZE]
  if want!=got:failures.append(dict(case=i,operation=HEADER_IN.unpack(rows[i][0])[0],firstByte=next(j for j,(x,y) in enumerate(zip(want,got)) if x!=y),original=want.hex(),native=got.hex()))
 receipt=dict(sampleSha256=m.SHA,weapon=a.weapon,cases=len(rows),nativeLatchChecks=6,glockCases=360,namedAllocationBoundaries=[x[0] for x in named],mismatchCount=len(failures),mismatches=failures[:3],originalRanges=r.ranges,inputSha256=hashlib.sha256(payload).hexdigest(),originalOutputSha256=hashlib.sha256(expected).hexdigest(),nativeOutputSha256=hashlib.sha256(actual).hexdigest(),gameplay=False,performance=False,scope='Selected original pistol emission and pool, plus Glock world AngleVectors/shell; bone transforms and host services modeled; no gameplay acceptance')
 a.output.write_text(json.dumps(receipt,indent=2));print(json.dumps({k:v for k,v in receipt.items() if k not in ('mismatches','originalRanges')}));assert not failures
if __name__=='__main__':main()
