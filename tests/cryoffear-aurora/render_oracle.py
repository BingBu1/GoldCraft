#!/usr/bin/env python3
"""Original Aurora draw instructions versus actual SDK adapter call arguments."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import random
import struct
import subprocess
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("aim_oracle",ROOT/"tests/cryoffear-aim/oracle.py")
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
INPUT=struct.Struct("<20f3i")
OUTPUT=struct.Struct("<25f6i")
P=base.HUD+0x1000
TYPE=base.HUD+0x2000
TRI=base.CVARS
MODEL=base.CVARS+0x200
RIGHT=base.CVARS+0x400
UP=base.CVARS+0x420
class Original(base.Original):
    def __init__(self,sample):
        super().__init__(sample)
        uc=self.uc
        # Same original constructor table expression, rounded to f32 on store.
        for i in range(450):uc.mem_write(0x105448D0+i*4,struct.pack("<f",math.sin(i*math.pi/180.0)))
        uc.mem_write(0x101B2750,base.u32(TRI))
        for slot,stub in ((4,0x200),(8,0x220),(12,0x240),(16,0x260),(24,0x280),(28,0x2A0),(44,0x2C0)):
            uc.mem_write(TRI+slot,base.u32(base.STUB+stub))
            uc.mem_write(base.STUB+stub,b"\xC3")
        uc.mem_write(0x101B2700,base.u32(base.STUB+0x2E0))
        uc.mem_write(base.STUB+0x2E0,b"\xB8"+base.u32(MODEL)+b"\xC3")
        self.uc.hook_add(UC_HOOK_CODE,self.service)
        self.ranges={f"{a:08x}-{b:08x}":hashlib.sha256(uc.mem_read(a,b-a)).hexdigest()
                     for a,b in ((0x10081920,0x10081D53),(0x10094770,0x100947BC))}
    def args(self,n):return struct.unpack("<"+"I"*n,self.uc.mem_read(self.uc.reg_read(UC_X86_REG_ESP)+4,n*4))
    def floats(self,n):return struct.unpack("<"+"f"*n,self.uc.mem_read(self.uc.reg_read(UC_X86_REG_ESP)+4,n*4))
    def service(self,uc,address,size,_):
        if address==base.STUB+0x200:self.ints[1]=self.args(1)[0]
        elif address==base.STUB+0x220:self.ints[2]=self.args(1)[0]
        elif address==base.STUB+0x240:self.ints[4]+=1
        elif address==base.STUB+0x260:self.values[1:5]=self.floats(4)
        elif address==base.STUB+0x280:self.values[17+self.ints[3]*2:19+self.ints[3]*2]=self.floats(2)
        elif address==base.STUB+0x2A0:
            self.values[5+self.ints[3]*3:8+self.ints[3]*3]=struct.unpack("<3f",uc.mem_read(self.args(1)[0],12))
            self.ints[3]+=1
        elif address==base.STUB+0x2C0:
            self.ints[0]=self.args(2)[1]
            uc.reg_write(UC_X86_REG_EAX,self.texture)
    def run(self,row):
        uc=self.uc
        self.values=[0.]*25;self.ints=[-777,-777,-777,0,0,0]
        floats=row[:20];state,frames,self.texture=row[20:]
        uc.mem_write(base.HUD,bytes(0x3000))
        uc.mem_write(P+8,base.u32(TYPE))
        uc.mem_write(P+0xC,struct.pack("<3f",*floats[:3]))
        uc.mem_write(P+0x40,struct.pack("<3f",*floats[3:6]))
        uc.mem_write(P+0x58,struct.pack("<f",floats[6]))
        uc.mem_write(P+0x70,struct.pack("<f",floats[7]))
        uc.mem_write(P+0x68,struct.pack("<f",floats[8]))
        uc.mem_write(P+0x60,struct.pack("<f",floats[9]))
        uc.mem_write(RIGHT,struct.pack("<3f",*floats[10:13]))
        uc.mem_write(UP,struct.pack("<3f",*floats[13:16]))
        uc.mem_write(base.DATA,struct.pack("<f",floats[16]))
        uc.mem_write(base.HUD+0x98,base.u32(state))
        uc.mem_write(base.HUD+0xA0,struct.pack("<2f",*floats[17:19]))
        uc.mem_write(TYPE+4,base.u32(5))
        uc.mem_write(TYPE+0x15C,base.u32(1))
        uc.mem_write(MODEL+0x48,base.u32(frames))
        sp=base.STACK+0x1000
        uc.mem_write(sp,b"".join(base.u32(v) for v in (base.STOP,P,RIGHT,UP)))
        uc.reg_write(UC_X86_REG_ESP,sp);uc.reg_write(UC_X86_REG_ECX,base.HUD)
        uc.emu_start(0x10081920,base.STOP,count=30000)
        self.values[0]=struct.unpack("<f",uc.mem_read(P+0x60,4))[0]
        self.ints[5]=1 if floats[7]==0 else (0 if self.texture else 2)
        return OUTPUT.pack(*self.values,*self.ints)
def cases():
    for angle in (-359.9,-180.,-91.,-90.,-1.,-0.,0.,45.,90.,180.,359.,360.):
        for frame in (-10.,-1.,-0.,0.,1.,10.):
            for state,now in ((0,0.),(1,1.),(1,1.5),(1,2.),(1,2.01)):
                yield (1.,2.,3.,.016,.02,.024,3.,20.,angle,frame,0.,1.,0.,0.,0.,1.,now,1.,1.,0.,state,1,1)
    rng=random.Random(0xA0A)
    for _ in range(2000):
        yield (*[rng.uniform(-1000,1000) for _ in range(3)],
               *[rng.uniform(-1,5) for _ in range(4)],rng.uniform(-100,100),rng.uniform(-359,360),rng.uniform(-20,20),
               *[rng.uniform(-1,1) for _ in range(6)],rng.uniform(0,5),rng.uniform(0,5),rng.uniform(.01,2),0.,rng.randrange(2),rng.choice((1,10)),rng.randrange(2))
    for size in (0.,-0.):yield (0.,0.,0.,1.,1.,1.,1.,size,0.,10.,0.,1.,0.,0.,0.,1.,0.,0.,1.,0.,0,1,1)
def main():
    p=argparse.ArgumentParser();p.add_argument("--sample",type=Path,required=True)
    p.add_argument("--runner",type=Path,required=True);p.add_argument("--output",type=Path,required=True)
    args=p.parse_args();original=Original(args.sample)
    payload=b"".join(INPUT.pack(*r) for r in cases());rows=list(INPUT.iter_unpack(payload))
    expected=b"".join(original.run(r) for r in rows)
    actual=subprocess.run([str(args.runner.resolve()),"--oracle"],input=payload,capture_output=True,check=True).stdout
    assert len(actual)==len(expected),(len(actual),len(expected))
    mismatches=[]
    for i,row in enumerate(rows):
        a=actual[i*OUTPUT.size:(i+1)*OUTPUT.size];e=expected[i*OUTPUT.size:(i+1)*OUTPUT.size]
        if a!=e:mismatches.append(dict(case=i,input=row,actual=list(struct.unpack("<31I",a)),expected=list(struct.unpack("<31I",e))))
    args.output.write_text(json.dumps(dict(sampleSha256=base.SHA,cases=len(rows),originalRanges=original.ranges,
        bitExactMismatches=len(mismatches),firstMismatches=mismatches[:4],gameplay=False,
        boundary="Explicit resolved zero controls only; no smoke activation or inferred seed"),indent=2),encoding="utf8")
    assert not mismatches,f"{len(mismatches)} Aurora render mismatches; see {args.output}"
    print(f"Aurora renderer: {len(rows)} original x86 cases, zero SDK argument/float-bit mismatches")
if __name__=="__main__":main()
