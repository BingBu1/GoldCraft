# SPDX-License-Identifier: GPL-3.0-or-later
"""Original multiplayer WeaponMelee/helper instruction execution."""
import importlib.util
from pathlib import Path
import random
import struct
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_MXCSR

# Shared PE mapping/callback plumbing only; no reconstructed melee algorithm.
spec = importlib.util.spec_from_file_location('bullet_instruction_fixture', Path(__file__).parents[1]/'cryoffear-ballistics/oracle.py')
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
Original, BASE, WORK, STACK, EDGE = shared.Original, shared.BASE, shared.WORK, shared.STACK, shared.EDGE
bits, number, f32 = shared.bits, shared.number, shared.f32

class Melee(Original):
    def __init__(self, sample):
        super().__init__(sample)
        self.rules = WORK+0xc0000
        self.write(0x10222ee4, self.rules)
        self.write(self.rules, self.rules+0x1000)
        self.write(self.rules+0x1020, EDGE+0x1000)
        self.write(0x10231f44, 29)
        self.write(0x1022671c, 78)
        self.write(self.globals+0x90, 3)
        for index in range(6):
            for offset in (0x50,):
                self.write(self.vtable(index)+offset, EDGE+0x2000+index*0x200+offset)
        self.write(self.vtable(1)+0x228,EDGE+0x6000)
        for offset in (0x4c,0x74,0x88,0x120,0x168,0x1ec):
            self.write(0x10222f48+offset, EDGE+offset)

    def trace(self, start, end, skip, address, hull):
        av, bv = self.vector(start), self.vector(end)
        if hull:
            self.event(19, self.index(skip,self.edict), 3, *av, *bv)
            fraction = 1.0 if self.mode==1 else .5
        else:
            self.line += 1
            self.event(2,self.index(skip,self.edict),*av,*bv)
            if self.line==1:
                fraction = .5 if self.mode in (0,6) else 1.0
            elif self.mode==3:
                fraction = .25
            elif self.mode==4 and self.line>2:
                fraction = (.8,.5,.75,.25,.5,.5,.4,.8)[self.line-3]
            else:
                fraction = 1.0
        self.uc.mem_write(address,bytes(56))
        self.write(address+0x10,bits(fraction))
        for i in range(3):
            a,b=number(av[i]),number(bv[i])
            self.write(address+0x14+i*4,bits(f32(a+f32(f32(b-a)*fraction))))
        self.write(address+0x2c,bits(1))
        self.write(address+0x30,self.edict(5 if self.mode==6 else 2))

    def hook(self,uc,address,size,user):
        if BASE <= address < BASE+0x300000:
            return super().hook(uc,address,size,user)
        sp=uc.reg_read(UC_X86_REG_ESP)
        arg=lambda n:self.read(sp+4+n*4)
        if address==EDGE+0x1000:
            self.ret(0,1)
        elif address==EDGE+0x4c:
            self.event(20,*self.vector(arg(0)))
            for i,value in enumerate(self.forward): self.write(self.globals+0x28+4*i,value)
            self.ret(0)
        elif address==EDGE+0x74:
            text=bytes(uc.mem_read(arg(2),80)).split(bytes([0]),1)[0].decode('ascii')
            names=[f'player/police/Swing/Swing{i}.wav' for i in range(1,7)]+['weapons/mobile/punch_swing.wav','weapons/mobile/punch_smack.wav']
            self.event(23,self.index(arg(0),self.edict),arg(1),names.index(text),arg(3),arg(4),arg(5),arg(6))
            self.ret(0)
        elif address==EDGE+0x7c:
            self.trace(arg(0),arg(1),arg(3),arg(4),False);self.ret(0)
        elif address==EDGE+0x88:
            if arg(2)!=0 or arg(3)!=3:raise ValueError('Original hull contract differs')
            self.trace(arg(0),arg(1),arg(4),arg(5),True);self.ret(0)
        elif address==EDGE+0x120:
            self.event(27,arg(0));self.ret(0,self.edict(arg(0)))
        elif address==EDGE+0x168:
            self.event(22,arg(0),arg(1),self.breath);self.ret(0,self.breath)
        elif address==EDGE+0x1ec:
            self.event(24,arg(0),self.index(arg(1),self.edict),arg(2),arg(3),*self.vector(arg(4)),*self.vector(arg(5)),*[arg(i) for i in range(6,12)])
            self.ret(0)
        elif address==EDGE+0x6000:
            self.event(21)
            for i,value in enumerate(self.source):self.write(arg(0)+4*i,value)
            self.ret(4,arg(0))
        elif address==EDGE+0xb8:
            self.event(6,arg(0),arg(1),self.index(arg(3),self.edict));self.ret(0)
        elif address>=EDGE+0x2000:
            entity,offset=divmod(address-EDGE-0x2000,0x200)
            if offset==0x50:
                value=self.classes[min(self.classcalls,1)];self.classcalls+=1
                self.event(25,entity,value);self.ret(0,value)
            elif offset==0xc0:
                bsp=self.mode in (3,4,5)
                self.event(26,entity,int(bsp));self.ret(0,int(bsp))
            elif offset==0x228:
                self.event(21)
                for i,value in enumerate(self.source):self.write(arg(0)+4*i,value)
                self.ret(4,arg(0))
            elif offset==0x60:
                self.write(0x10231f3c,arg(6))
                return super().hook(uc,address,size,user)
            else:return super().hook(uc,address,size,user)
        else:return super().hook(uc,address,size,user)

    def run(self,row):
        self.events,self.last,self.line,self.classcalls=[],[],0,0
        self.source,self.forward=row[:3],row[6:9]
        self.breath,self.mode,self.classes=row[14],row[15],row[16:18]
        for i,v in enumerate(row[3:6]):self.write(self.pev(1)+0x74+4*i,v)
        for e in range(1,4):
            for i,v in enumerate(row[9:12]):self.write(self.pev(e)+8+4*i,v)
            self.write(self.pev(e)+0x1a8,512 if row[18]&(1<<(e-1)) else 0)
            self.write(self.edict(e),int(e==3 and row[20]))
            self.write(self.edict(e)+0x7c,0 if e==3 and row[21] else self.obj(e))
        self.write(self.pev(2)+8,bits(f32(number(row[9])+number(row[19]))))
        self.write(self.edict(5)+0x7c,0)
        self.write(0x10226834,bits(row[12]))
        self.uc.mem_write(0x10226489,bytes([row[13]]))
        self.write(self.weapon+0x168,1)
        stack=STACK+0x8000
        self.write(stack,0x60000000)
        self.uc.reg_write(UC_X86_REG_ESP,stack)
        self.uc.reg_write(UC_X86_REG_ECX,self.obj(1))
        self.uc.reg_write(UC_X86_REG_MXCSR,0x1f80)
        try:self.uc.emu_start(0x100f9790,0x60000000,count=30000)
        except Exception as error:
            raise RuntimeError(f'Original stopped {self.uc.reg_read(UC_X86_REG_EIP):08x}, last={self.last}, events={self.events[-20:]}') from error
        if self.uc.reg_read(UC_X86_REG_EIP)!=0x60000000:raise ValueError('Original failed to return')
        return [2 if self.mode==1 else 1,*self.events]

def generate(sample,cases,expected):
    original=Melee(sample)
    rng=random.Random(0x100f9790)
    for i in range(512):
        row=[bits(rng.uniform(-128,128)) for _ in range(3)]
        row += [bits(rng.uniform(-180,180)) for _ in range(3)]
        row += [bits(rng.uniform(-1,1)) for _ in range(3)]
        row += [bits(rng.uniform(-128,128)) for _ in range(3)]
        row += [(0,1,15,16,35,100,1000)[i%7],i%2,i%6,i%7,(i//7)%4,(i//28)%4,(i//3)%8,
                (0x3fffffff,0x40000000,0x40000001,0x3f800000)[(i//9)%4],(i//11)%2,(i//13)%2]
        if i<16:row[9:12]=[bits(0),bits(0),bits(0)]
        if i>=128 and i%8<4:row[19]=(0x7fc12345,0x7f800000,0xff800000,0xc0000000)[i%8]
        result=original.run(row)
        cases.write(' '.join(f'{v:08x}' for v in row)+'\n')
        expected.write(' '.join(f'{v:08x}' for v in result)+'\n')
    required=(0x100f98cf,0x100df8a7,0x100df956,0x100f99e4,0x100f99ec,0x100f9a31,0x100f9a97,0x10119eeb)
    if not all(v in original.visited for v in required):raise ValueError('Required original branches missing')
    return dict(sampleSha256=shared.SHA,functionVA='100F9790',cases=512,visitedInstructions=len(original.visited),
                requiredInstructions=[f'{v:08x}' for v in required],boundary='Original multiplayer WeaponMelee and original helpers, controlled engine/entity/recoil edges')
