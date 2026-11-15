# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute complete original stamina cycle and scalar Spawn/HUD subsections."""
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import struct
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EDI,UC_X86_REG_ESI,UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_MXCSR,UC_X86_REG_FPCW,UC_X86_REG_XMM2,UC_X86_REG_XMM3,UC_X86_REG_XMM4
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('cof_cycle_image',ROOT/'tests/cryoffear-player-think/oracle.py')
shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)
bits,number=shared.bits,shared.number
WORK,OWNER,PEV,TIME,STACK,EDGE=0x30000000,0x30001000,0x30006000,0x30008000,0x40008000,0x50000000
OFFSETS=(0x21f0,0x21ec,0x21f4,0x21f8,0x3c0,0x1e94,0x1eec,0x430)
class Original:
    def __init__(self,sample):
        self.uc=Uc(UC_ARCH_X86,UC_MODE_32);base,data=shared.image(sample)
        self.uc.mem_map(base,(len(data)+4095)&~4095);self.uc.mem_write(base,bytes(data))
        for at,size in ((WORK,0x10000),(STACK-0x8000,0x10000),(EDGE,0x1000)):self.uc.mem_map(at,size)
        self.uc.mem_write(EDGE,b'\xc3'*0x1000)
        self.write(OWNER+4,PEV);self.write(0x10223188,TIME)
        self.write(WORK+0x9000,WORK+0xa000);self.write(WORK+0xa020,EDGE+0x10)
        for pointer,edge in ((0x102230b0,0x20),(0x10223000,0x40),(0x10223008,0x50),(0x1022301c,0x60),(0x10223004,0x70)):
            self.write(pointer,EDGE+edge)
        self.write(0x1022657c,123);self.write(PEV+0x20c,WORK+0x4000)
        self.uc.hook_add(UC_HOOK_CODE,self.hook)
    def write(self,at,value):self.uc.mem_write(at,struct.pack('<I',value&0xffffffff))
    def read(self,at):return struct.unpack('<I',self.uc.mem_read(at,4))[0]
    def ret(self,result=0):
        sp=self.uc.reg_read(UC_X86_REG_ESP);target=self.read(sp)
        self.uc.reg_write(UC_X86_REG_EAX,result);self.uc.reg_write(UC_X86_REG_ESP,sp+4);self.uc.reg_write(UC_X86_REG_EIP,target)
    def flags(self):
        return int(bool(self.read(OWNER+0x21e8)&255))|int(bool(self.read(OWNER+0x3bc)&255))<<1|int(bool(self.read(OWNER+0x1ee0)&255))<<2|int(bool(self.read(0x10222ee4)))<<3
    def snapshot(self):return [self.read(OWNER+o) for o in OFFSETS]+[self.read(PEV+0x1a8),self.read(TIME),self.flags(),self.read(PEV+0x268),self.read(PEV+0x1c4)]
    def observe(self,kind,*args):
        self.events.extend((kind,len(args),*args,*self.snapshot()));self.ordinal+=1
        if self.ordinal!=self.nth:return
        if self.change&1:self.write(TIME,bits(number(self.read(TIME))+number(self.delta)))
        if self.change&2:self.write(OWNER+0x21f0,self.new_amount)
        if self.change&4:self.uc.mem_write(OWNER+0x1ee0,bytes([int(not(self.read(OWNER+0x1ee0)&255))]))
        if self.change&8:self.write(OWNER+0x430,self.new_pulse)
        if self.change&16:self.write(PEV+0x1a8,self.read(PEV+0x1a8)^0x80000)
        if self.change&32:self.write(PEV+0x268,bits(1-number(self.read(PEV+0x268))))
    def hook(self,uc,addr,size,user):
        sp=uc.reg_read(UC_X86_REG_ESP);arg=lambda n:self.read(sp+4+n*4)
        if addr==EDGE+0x10:self.observe(1);self.ret(self.multi)
        elif addr==EDGE+0x20:
            if (arg(0),arg(1))!=(0,1):raise ValueError('Original RNG arguments')
            self.observe(2,0,1);self.ret(self.random)
        elif addr==0x100fc9f0:
            samples={0x101a9330:0,0x101a9254:1,0x101a9404:2,0x101a942c:3}
            if arg(0)!=WORK+0x4000:raise ValueError('Original sound owner')
            self.observe(3,samples[arg(2)],arg(1),arg(3),arg(4),arg(5),arg(6));self.ret()
        elif addr==EDGE+0x40:
            if (arg(1),arg(2),arg(3))!=(123,0,WORK+0x4000):raise ValueError('Original HUD route')
            self.observe(4,arg(0));self.ret()
        elif addr==EDGE+0x50:self.observe(5,arg(0));self.ret()
        elif addr==EDGE+0x60:self.observe(6,arg(0));self.ret()
        elif addr==EDGE+0x70:self.observe(7);self.ret()
    def run(self,r):
        mode,now,*rest=r;self.events=[];self.ordinal=0
        for off,value in zip(OFFSETS,r[2:10]):self.write(OWNER+off,value)
        self.write(TIME,now);self.write(PEV+0x1a8,r[10]);self.write(PEV+0x268,r[12]);self.write(PEV+0x1c4,r[13])
        for off,flag in ((0x21e8,1),(0x3bc,2),(0x1ee0,4)):self.uc.mem_write(OWNER+off,bytes([int(bool(r[14]&flag))]))
        self.write(0x10222ee4,WORK+0x9000 if r[14]&8 else 0)
        self.multi,self.random,self.change,self.nth,self.delta,self.new_amount,self.new_pulse=r[15:22]
        self.uc.reg_write(UC_X86_REG_ESP,STACK);self.uc.reg_write(UC_X86_REG_ESI,OWNER)
        self.uc.reg_write(UC_X86_REG_EDI,OWNER);self.uc.reg_write(UC_X86_REG_ECX,OWNER);self.uc.reg_write(UC_X86_REG_EDX,0)
        self.uc.reg_write(UC_X86_REG_XMM2,0);self.uc.reg_write(UC_X86_REG_XMM3,bits(1));self.uc.reg_write(UC_X86_REG_XMM4,r[11])
        self.uc.reg_write(UC_X86_REG_MXCSR,0x1f80);self.uc.reg_write(UC_X86_REG_FPCW,0x27f)
        start,end=(0x100f468d,0x100f4724) if mode else (0x100f539b,0x100f56f5)
        try:self.uc.emu_start(start,end,count=2000)
        except Exception as e:raise ValueError(f'Original region failed at {self.uc.reg_read(UC_X86_REG_EIP):08x}; row {r}') from e
        if self.uc.reg_read(UC_X86_REG_EIP)!=end:raise ValueError('Original cycle did not reach boundary')
        return self.snapshot()+self.events

def generate(sample,proof,cases,expected):
    pre=json.loads(proof.read_text('utf-8-sig'))
    if not pre['beforeImplementation'] or shared.sha(sample)!=shared.MP_SHA or pre['sampleSha256']!=shared.MP_SHA:
        raise ValueError('Original stamina proof identity')
    base,data=shared.image(sample)
    for region in pre['regions']:
        at=int(region['va'],16)-base
        if hashlib.sha256(data[at:at+region['size']]).hexdigest()!=region['sha256']:raise ValueError('Original stamina region')
    for name,digest in pre['archives'].items():
        if shared.sha(proof.parent/name)!=digest:raise ValueError('IDA archive changed')
    original=Original(sample);rng=random.Random(100539);count=0
    def emit(r):
        nonlocal count
        answer=original.run(r);cases.write(' '.join(format(v,'x') for v in r)+'\n')
        expected.write(' '.join(format(v,'x') for v in answer)+'\n');count+=1
    def row(now,amount,next,flags=0,modes=8):
        return [0,bits(now),bits(amount),bits(50),bits(next),bits(3),bits(7),bits(0),bits(9),bits(.125),
                flags,bits(1),0,0,modes,1,0,0,1,bits(.375),bits(75),bits(.25)]
    for now in (.1,10.125,8192.3,65536.125):
        now=number(bits(now))
        for amount in (-5,0,.1,.99,1,2,99,100,101):
            for delta in (-1.,0.,1.):
                for flags in (0,0x4000,0x40000,0x80000):
                    for modes in range(8):emit(row(now,amount,number(bits(now+delta)),flags,modes|8))
    for water in (0,2,3):
        for rules in (0,8):
            for multi in (0,1):
                for voice in (0.,1.):
                    for choice in (0,1):
                        r=row(10,0,9);r[12]=bits(voice);r[13]=water;r[14]=rules;r[15]=multi;r[16]=choice
                        emit(r)
    for mode in (0,1):
        for nth in range(1,8):
            for change in range(64):
                r=row(10,0,9);r[0]=mode;r[17]=change;r[18]=nth;emit(r)
    for _ in range(256):
        r=row(rng.uniform(.1,1e5),rng.uniform(-5,110),rng.uniform(.1,1e5),rng.choice((0,0x4000,0xc0000)),rng.randrange(16))
        r[0]=rng.randrange(2);r[3]=r[2] if rng.randrange(2) else bits(50);r[9]=rng.choice((bits(0.),bits(-0.),bits(.25)))
        emit(r)
    return dict(cases=count,sampleSha256=shared.MP_SHA,proofSha256=shared.sha(proof),boundary='Complete original858B cycle and151B scalar Spawn/HUD subsection; no movement prefix or FOV/water tail; sound wrapper/message transports controlled')
