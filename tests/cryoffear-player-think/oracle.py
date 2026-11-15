# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay original CoF MP scheduling and complete engine PlayerRunThink."""
import hashlib
import json
from pathlib import Path
import random
import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_MXCSR

MP_SHA='0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
ENGINE_SHA='9dd34e536c4bb7cda3bc1bc4f0f5f6163687566a16f35c0ea0be59f93da62875'
WORK,OWNER,EDICT,TIME,STACK,EDGE=0x30000000,0x30001000,0x30004000,0x30007000,0x40008000,0x50000000

def bits(x):return struct.unpack('<I',struct.pack('<f',x))[0]
def number(x):return struct.unpack('<f',struct.pack('<I',x))[0]
def doublebits(x):return struct.unpack('<Q',struct.pack('<d',x))[0]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def image(path):
    raw=path.read_bytes();pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24
    if raw[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',raw,pe+4)[0]!=0x14c:raise ValueError('Expected x86 PE')
    base=struct.unpack_from('<I',raw,opt+28)[0];out=bytearray(struct.unpack_from('<I',raw,opt+56)[0])
    at=opt+struct.unpack_from('<H',raw,pe+20)[0]
    for n in range(struct.unpack_from('<H',raw,pe+6)[0]):
        rva,size,offset=struct.unpack_from('<III',raw,at+n*40+12);out[rva:rva+size]=raw[offset:offset+size]
    return base,out

class Original:
    def __init__(self,mp,engine):
        self.uc=Uc(UC_ARCH_X86,UC_MODE_32)
        for path in (mp,engine):
            base,data=image(path);self.uc.mem_map(base,(len(data)+4095)&~4095);self.uc.mem_write(base,bytes(data))
        for at,size in ((WORK,0x10000),(STACK-0x8000,0x10000),(EDGE,0x1000)):self.uc.mem_map(at,size)
        self.uc.mem_write(EDGE,b'\xc3'*0x1000)
        self.write(OWNER+4,EDICT+0x80);self.write(0x10223188,TIME);self.write(0x250d8c8,EDGE+0x100)
        self.uc.hook_add(UC_HOOK_CODE,self.hook)
    def write(self,p,v):self.uc.mem_write(p,struct.pack('<I',v&0xffffffff))
    def read(self,p):return struct.unpack('<I',self.uc.mem_read(p,4))[0]
    def ret(self,pop=0):
        sp=self.uc.reg_read(UC_X86_REG_ESP);target=self.read(sp)
        self.uc.reg_write(UC_X86_REG_ESP,sp+4+pop);self.uc.reg_write(UC_X86_REG_EIP,target)
    def hook(self,uc,addr,size,user):
        if addr==EDGE+0x100:
            self.events.extend((self.read(OWNER+0x78),self.read(EDICT+0x184),self.read(OWNER+0x58),self.read(0x251f3c0)))
            self.calls+=1
            if self.mode and self.calls==1:
                # Invoke the actual MP scheduler from the controlled Think ABI.
                self.write(TIME,self.read(0x251f3c0));sp=uc.reg_read(UC_X86_REG_ESP)-8
                self.write(sp,EDGE+0x110);self.write(sp+4,self.redelay)
                uc.reg_write(UC_X86_REG_ESP,sp);uc.reg_write(UC_X86_REG_ECX,OWNER);uc.reg_write(UC_X86_REG_EIP,0x1011f680)
            else:self.ret()
        elif addr==EDGE+0x110:
            self.write(OWNER+0x78,self.mode);self.ret()
        elif addr==0x1da7b43:self.freed+=1;self.ret() # Native entity-free boundary only.
    def execute(self,addr,args,ecx=0):
        self.uc.reg_write(UC_X86_REG_FPCW,0x27f);self.uc.reg_write(UC_X86_REG_MXCSR,0x1f80)
        self.uc.reg_write(UC_X86_REG_ECX,ecx);self.uc.reg_write(UC_X86_REG_ESP,STACK)
        self.write(STACK,EDGE+0xff0);self.uc.mem_write(STACK+4,args)
        self.uc.emu_start(addr,EDGE+0xff0,count=2000)
        if self.uc.reg_read(UC_X86_REG_EIP)!=EDGE+0xff0:raise ValueError('Original scheduler did not return')
    def snapshot(self):return [self.read(EDICT+0x184),self.read(OWNER+0x58),self.read(0x251f3c0),self.read(OWNER+0x78),self.calls]
    def run(self,row):
        now,delay,frame,base,flags,self.mode,self.redelay,frame2,base2=row
        self.calls=self.freed=0;self.events=[]
        self.write(EDICT,0);self.write(EDICT+0x228,flags);self.write(TIME,now);self.write(0x251f3c0,now)
        self.execute(0x1011f680,struct.pack('<I',delay),OWNER);self.write(OWNER+0x78,1)
        out=self.snapshot()
        for f,b in ((frame,base),(frame2,base2)):
            self.execute(0x1deee49,struct.pack('<IIQ',EDICT,f,b))
            out+=self.snapshot()
        return out+self.events

def generate(mp,engine,proof,cases,expected):
    pre=json.loads(proof.read_text('utf-8-sig'));fp=proof.parent/'FP-PRECISION-SUPPLEMENT.json'
    precision=json.loads(fp.read_text('utf-8-sig'))
    if not pre['beforeImplementation'] or sha(mp)!=MP_SHA or sha(engine)!=ENGINE_SHA or pre['mpSha256']!=MP_SHA or pre['engineSha256']!=ENGINE_SHA:
        raise ValueError('Original scheduler identity mismatch')
    if precision['engineSha256']!=ENGINE_SHA or precision['defaultControlWord']!='027f':raise ValueError('Original precision mismatch')
    for path,regions in ((mp,pre['mpRegions']),(engine,pre['engineRegions']+precision['regions'])):
        base,data=image(path)
        for r in regions:
            start=int(r['va'],16)-base
            if hashlib.sha256(data[start:start+r['size']]).hexdigest()!=r['sha256']:raise ValueError('Original region changed')
    for name,digest in pre['archives'].items():
        if sha(proof.parent/name)!=digest:raise ValueError('IDA evidence changed')
    original=Original(mp,engine);rows=[];rng=random.Random(161511)
    for now in (0.,.1,10.,8192.3,65536.125,1048576.):
        for delay in (-1.,0.,.001,.23,.5,1.):
            due=number(bits(number(bits(now))+number(bits(delay))))
            for delta in (-.02,0.,.02):
                for mode in (0,1,2):
                    rows.append([bits(now),bits(delay),bits(.01),doublebits(due-.01+delta),0,mode,bits(.25),bits(.01),doublebits(due+.5)])
    for flags in (0,0x40000000,0x80000000,0xc0000000):
        for mode in range(3):rows.append([bits(10),bits(.23),bits(.01),doublebits(10.23),flags,mode,bits(.1),bits(.05),doublebits(11)])
    # Near-equality double clienttimebases expose an accidental float sum.
    for _ in range(900):
        now=number(bits(rng.uniform(.1,1e5)));delay=number(bits(rng.uniform(.001,1)))
        due=number(bits(now+delay));frame=number(bits(rng.uniform(.001,.05)))
        base=due-frame+rng.choice((-1.,0.,1.))*2.**-40
        rows.append([bits(now),bits(delay),bits(frame),doublebits(base),0,rng.randrange(3),bits(.23),bits(.05),doublebits(due+1)])
    for row in rows:
        answer=original.run(row)
        cases.write(' '.join(format(v,'x') for v in row)+'\n')
        expected.write(' '.join(format(v,'x') for v in answer)+'\n')
    return dict(cases=len(rows),mpSha256=MP_SHA,engineSha256=ENGINE_SHA,proofSha256=sha(proof),precisionSha256=sha(fp),x87ControlWord='027f',boundary='Original MP two scheduling functions and complete engine196B; Think consumer/native freeing controlled; no live engine-stage integration')
