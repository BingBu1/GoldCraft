# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute full original common Post around its isolated M16 input-dispatch stage."""
import hashlib
import importlib.util
import json
from pathlib import Path
import random
from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP,UC_X86_REG_MXCSR
ROOT=Path(__file__).resolve().parents[2]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
shared=load('m16_input_abi',ROOT/'tests/cryoffear-p345-weapon/oracle.py')
BASE,WORK,STACK,EDGE=shared.BASE,shared.WORK,shared.STACK,shared.EDGE
bits,number=shared.bits,shared.number
SHA='0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
class Input(shared.P345):
    def __init__(self,sample):
        super().__init__(sample)
        vt=WORK+0xa0000
        for off,addr in ((440,0x1001e930),(444,0x1001e0c0),(460,0x1001ec60),(464,0x1001f050),(468,EDGE+0x7500)):
            self.write(vt+off,addr)
        for kind,off in ((1,420),(2,424),(3,428),(4,448)):self.write(vt+off,EDGE+0x7600+kind*16)
        self.last=[]
        # Populate the actual original item table through M16 GetItemInfo.
        self.uc.reg_write(UC_X86_REG_ESP,STACK+0xff00);self.write(STACK+0xff00,EDGE+0xff00)
        self.write(STACK+0xff04,0x10231888+44*9);self.uc.reg_write(UC_X86_REG_ECX,self.weapon)
        self.uc.emu_start(0x10050e40,EDGE+0xff00,count=1000)
        if self.uc.reg_read(UC_X86_REG_EIP)!=EDGE+0xff00:raise ValueError('Original item metadata did not return')

    def snapshot(self):
        return [self.read(self.weapon+o) for o in (0x1e4,0x1cc,0x1d0)]+[self.read(self.obj(1)+0x308),
            self.read(self.weapon+0x1b8),self.read(self.pev(1)+0x184),self.read(self.pev(1)+0x260),
            self.read(self.pev(1)+0x108),self.read(self.globals)]
    def observe(self,kind):
        self.events += [kind]+self.snapshot();self.ordinal+=1
        if self.ordinal!=self.nth:return
        if self.change&1:self.write(self.globals,bits(number(self.read(self.globals))+number(self.delta)))
        if self.change&2:self.write(self.pev(1)+0x184,self.alt_buttons)
        if self.change&4:self.write(self.pev(1)+0x108,self.alt_move)
        if self.change&8:self.write(self.pev(1)+0x260,bits(1))
        if self.change&16:self.write(self.weapon+0x1e4,75)
    def hook(self,uc,address,size,user):
        if address==EDGE+0x7500:
            self.write(self.globals,bits(number(self.read(self.globals))+number(self.pre_advance)))
            if self.pre_buttons!=0xffffffff:self.write(self.pev(1)+0x184,self.pre_buttons)
            self.ret(0);return
        if address==0x100f5eb0:self.observe(0);self.ret(0);return
        if EDGE+0x7610<=address<=EDGE+0x7640:
            self.observe((address-EDGE-0x7600)//16);self.ret(0);return
        return super().hook(uc,address,size,user)
    def run(self,row):
        now,clip,primary,secondary,owner,loading,buttons,activity,move,empty,self.change,self.nth,self.delta,self.alt_buttons,self.alt_move,self.pre_advance,self.pre_buttons=row
        self.events=[];self.ordinal=0;self.last=[]
        self.write(self.globals,now)
        for o,v in ((0x168,9),(0x1e4,clip),(0x1cc,primary),(0x1d0,secondary),(0x1f0,loading),(0x1b8,empty),
            (0x1a4,0),(0x120,30),(0x1dc,1),(0x14c,100),(0x150,0),(0x154,bits(1e6))):self.write(self.weapon+o,v)
        self.write(self.obj(1)+9536+4,30);self.write(self.obj(1)+0x308,owner)
        for o,v in ((0x184,buttons),(0x260,activity),(0x108,move)):self.write(self.pev(1)+o,v)
        self.uc.reg_write(UC_X86_REG_ESP,STACK+0xff00);self.write(STACK+0xff00,EDGE+0xff00)
        self.uc.reg_write(UC_X86_REG_ECX,self.weapon);self.uc.reg_write(UC_X86_REG_MXCSR,0x1f80)
        self.uc.emu_start(0x1011d560,EDGE+0xff00,count=10000)
        if self.uc.reg_read(UC_X86_REG_EIP)!=EDGE+0xff00:raise ValueError('Original common did not return')
        return self.snapshot()+self.events

def generate(sample,proof,cases,expected):
    pre=json.loads(proof.read_text('utf-8-sig'))
    if hashlib.sha256(sample.read_bytes()).hexdigest()!=SHA or pre['sampleSha256']!=SHA or not pre['beforeImplementation']:
        raise ValueError('Original/pre-edit identity mismatch')
    mapper=load('m16_input_mapper',ROOT/'tests/cryoffear-magazine-actions/run.py');image=mapper.mapped_image(sample.read_bytes())
    for region in pre['regions']:
        at=int(region['va'],16)-BASE
        if hashlib.sha256(image[at:at+region['size']]).hexdigest()!=region['sha256']:raise ValueError('Original region changed')
    for name,digest in pre['archives'].items():
        if hashlib.sha256((proof.parent/name).read_bytes()).hexdigest()!=digest:raise ValueError('IDA evidence changed')
    original=Input(sample);count=0;rng=random.Random(1611)
    def emit(row):
        nonlocal count
        answer=original.run(row)
        cases.write(' '.join(f'{int(v):08x}' for v in row)+'\n')
        expected.write(' '.join(f'{v:08x}' for v in answer)+'\n');count+=1
    for now in (.1,13.37,8192.3,65536.125):
        for buttons in (0,1,0x40,0x800,0x2000,0x841,0x2001,0x2841):
            for move in (3,5):
                for activity in (-1,0,1):
                    for primary,secondary,owner in ((-1,-1,-1),(0,0,0),(1,1,1),(-1,1,1),(1,-1,1),(1,1,-1)):
                        emit([bits(now),rng.choice((0,1,20,75,511,10000)),bits(now+primary),bits(now+secondary),bits(now+owner),
                            0,buttons,bits(activity),move,rng.randrange(2),0,1,bits(.375),0,5,0,0xffffffff])
        for buttons in (1,0x40,0x800,0x2000,0x841):
            for change in range(32):
                emit([bits(now),0,bits(now-.5),bits(now-.5),bits(now-.5),0,buttons,0,3,0,change,1,bits(.375),0x2000,5,0,0xffffffff])
        # Pre-input consumers can move time past a previously-future attack gate.
        for buttons in (1,0x40,0x800):
            emit([bits(now),20,bits(now+.25),bits(now+.25),bits(now+.25),0,0,0,3,0,0,1,0,0,3,bits(.5),buttons])
        # Loading remains pending only while ownerAttack is still in the future.
        for buttons in (0,1,0x40,0x800,0x2000,0x2841):
            emit([bits(now),0,bits(now-1),bits(now-1),bits(now+1),1,buttons,0,3,1,0,1,0,0,3,0,0xffffffff])
    return dict(cases=count,sampleSha256=SHA,function='1011D560',bytes=2055,
        boundary='Complete original common instructions with nondual M16, no loop latch, stable wear/reserve and no due reload; input callbacks controlled. Pre-input time/button mutation exercised; no complete common-state or gameplay acceptance.')
