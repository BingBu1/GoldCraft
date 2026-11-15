# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the full1825-byte original derived frame; common frame is an explicit edge."""
import hashlib
import importlib.util
import json
from pathlib import Path
import random
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_MXCSR

ROOT=Path(__file__).resolve().parents[2]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
shared=load('m16_frame_abi',ROOT/'tests/cryoffear-p345-weapon/oracle.py')
BASE,WORK,STACK,EDGE=shared.BASE,shared.WORK,shared.STACK,shared.EDGE
bits,number=shared.bits,shared.number
SHA='0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
OFFSETS=(0x128,0x178,0x1a4,0x1f0,0x1cc,0x1d0,0x1d4,0x1d8,0x12c)

class Frame(shared.P345):
    def __init__(self,sample):
        super().__init__(sample)
        self.write(WORK+0xa0000+0x130,EDGE+0x7300)
        self.write(0x10223070,EDGE+0x128)
        self.model=WORK+0xc0000
        self.write(self.model+164,38);self.write(self.model+168,244)
        for i in range(38):
            self.write(self.model+244+i*176+32,bits(8))
            self.write(self.model+244+i*176+56,2+i)
    def snapshot(self):
        return [self.read(self.weapon+o) for o in OFFSETS]+[self.read(self.globals),self.read(self.pev(1)+0x1a8),
            self.read(self.pev(1)+0x20),self.read(self.pev(1)+0x24),self.read(self.pev(1)+0x21c)]
    def observe(self,kind,arg=0):
        self.events += [kind,arg]+self.snapshot();self.ordinal+=1
        if self.ordinal!=self.nth:return
        if self.change&1:self.write(self.globals,bits(number(self.read(self.globals))+number(self.delta)))
        if self.change&2:self.write(self.pev(1)+0x1a8,self.read(self.pev(1)+0x1a8)^0x40000)
        if self.change&4:
            self.write(self.pev(1)+0x20,0);self.write(self.pev(1)+0x24,0)
        if self.change&8:self.write(self.pev(1)+0x21c,self.alt)
        if self.change&16:self.write(self.pev(1)+0x1a8,0x40000)
        if self.change&32:self.write(self.pev(1)+0x20,bits(20))
    def hook(self,uc,address,size,user):
        sp=uc.reg_read(UC_X86_REG_ESP);arg=lambda n:self.read(sp+4+n*4)
        if address==EDGE+0x7198:
            self.write(self.pev(1)+0x21c,arg(0));self.observe(0,arg(0));self.ret(12);return
        if address==EDGE+0x7300:self.observe(1);self.ret(0);return
        if address==EDGE+0x71c0:
            self.observe(2);self.write(self.weapon+0x1f0,1);self.write(self.weapon+0x128,2);self.ret(0);return
        if address==EDGE+0x128:self.ret(0,self.model);return
        if address==0x1000f390:
            self.observe(3,arg(1)) # execute real sequence duration helper after observation/mutation
            return
        if address==0x1011d560:self.observe(4);self.ret(0);return
        return super().hook(uc,address,size,user)
    def run(self,row):
        now,action,aim,pending,loading,primary,secondary,extra,idle,deadline,flags,vx,vy,jump,gate,gate_time,self.change,self.nth,self.delta,self.alt=row
        self.events=[];self.ordinal=0;self.last=[]
        for o,v in zip(OFFSETS,(action,aim,pending,loading,primary,secondary,extra,idle,deadline)):self.write(self.weapon+o,v)
        self.write(self.weapon+0x140,0);self.write(self.globals,now)
        for o,v in ((0x1a8,flags),(0x20,vx),(0x24,vy),(0x21c,0)):self.write(self.pev(1)+o,v)
        for o,v in ((0x3aa,jump),(0x1ee8,gate),(0x1ee4,gate_time)):self.write(self.obj(1)+o,v)
        self.uc.reg_write(UC_X86_REG_ESP,STACK+0xff00);self.write(STACK+0xff00,EDGE+0xff00)
        self.uc.reg_write(UC_X86_REG_ECX,self.weapon);self.uc.reg_write(UC_X86_REG_MXCSR,0x1f80)
        self.uc.emu_start(0x10051540,EDGE+0xff00,count=10000)
        if self.uc.reg_read(UC_X86_REG_EIP)!=EDGE+0xff00:raise ValueError('Original frame failed to return')
        return self.snapshot()+self.events

def generate(sample,proof,cases,expected):
    pre=json.loads(proof.read_text('utf-8-sig'))
    if hashlib.sha256(sample.read_bytes()).hexdigest()!=SHA or pre['sampleSha256']!=SHA or not pre['beforeImplementation']:
        raise ValueError('Original/pre-edit identity mismatch')
    mapper=load('m16_frame_mapper',ROOT/'tests/cryoffear-magazine-actions/run.py')
    image=mapper.mapped_image(sample.read_bytes())
    for region in pre['regions']:
        start=int(region['va'],16)-BASE
        if hashlib.sha256(image[start:start+region['size']]).hexdigest()!=region['sha256']:raise ValueError('Original region changed')
    for path,digest in pre['archives'].items():
        if hashlib.sha256((proof.parent/path).read_bytes()).hexdigest()!=digest:raise ValueError('IDA archive changed')
    original=Frame(sample);count=0;rng=random.Random(16051540)
    def emit(row):
        nonlocal count
        answer=original.run(row)
        cases.write(' '.join(f'{int(v):08x}' for v in row)+'\n')
        expected.write(' '.join(f'{v:08x}' for v in answer)+'\n');count+=1
    for now in (.1,13.37,8192.3,65536.125):
        for action,flags,vx,vy,jump in ((0,0,0,0,0),(0,0x40000,3,4,0),(1,0x40000,3,4,0),
            (1,0,0,0,0),(0,0,3,4,1),(5,0x40000,3,4,0),(2,0x40000,3,4,0)):
            for change in (0,1,2,3,4,5,8,9,10,11,16,17,32,33):
                for nth in (1,2,3):
                    emit([bits(now),action,0,0,0,0,0,0,0,0,flags,bits(vx),bits(vy),jump,0,0,change,nth,bits(.375),17])
        for aim in (0,1):
            for gate in (0,1):
                for change in (0,1,2,9):
                    emit([bits(now),0,aim,1,0,0,0,0,0,0,0x40000,bits(3),bits(4),0,gate,0,change,1,bits(.375),17])
    for _ in range(1024):
        now=rng.choice((.1,13.37,8192.3,65536.125));clock=lambda:bits(now+rng.choice((-1,0,1)))
        emit([bits(now),rng.choice((0,1,2,5)),rng.randrange(2),rng.randrange(2),rng.randrange(2),
            clock(),clock(),clock(),clock(),clock(),rng.choice((0,0x40000)),bits(rng.choice((0,3,.01))),
            bits(rng.choice((0,4,.02))),rng.randrange(2),rng.randrange(2),clock(),rng.randrange(64),
            rng.randrange(1,4),bits(.375),rng.choice((0,14,16,17,38))])
    return dict(cases=count,sampleSha256=SHA,function='10051540',bytes=1825,
        boundary='Complete ordinary derived frame; original sequence helper/condition execute; aim/reload and common frame are controlled explicit consumers. No special1/2, full common frame or gameplay acceptance.')
