"""Execute original Glock PrimaryAttack; previously verified helpers are edges."""
import importlib.util
from pathlib import Path
import random
import struct
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_MXCSR

spec = importlib.util.spec_from_file_location('cof_bullet_fixture', Path(__file__).parents[1]/'cryoffear-ballistics/oracle.py')
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
BASE, WORK, STACK, EDGE = shared.BASE, shared.WORK, shared.STACK, shared.EDGE
bits, number = shared.bits, shared.number

class Glock(shared.Original):
    def __init__(self, sample):
        super().__init__(sample)
        self.write(self.weapon, WORK+0xa0000)
        self.write(self.weapon+4, self.pev(4))
        self.write(self.weapon+0x160, self.obj(1))
        self.write(self.weapon+0x168, 3)
        self.write(self.weapon+0x198, bits(1.25))
        self.write(self.weapon+0x1f8, 31)
        self.write(self.pev(4)+0x20c,self.edict(4))
        for off in (0x190,0x198,0x1c0): self.write(WORK+0xa0000+off,EDGE+0x7000+off)
        for off in (0x104,0x228): self.write(self.vtable(1)+off,EDGE+0x6000+off)
        for off in (0xb8,0xbc,0xc0,0xc8,0xcc,0xd0,0xd4,0xd8,0xdc,0x168,0x11c,0x1ec):
            self.write(0x10222f48+off,EDGE+off)
        self.write(0x10231f50,35)
        self.edges={0x1011cf70,0x10119320,0x100ea240,0x100f91b0,0x10119de0,
                    0x100f2a50,0x100dfea0,0x10088ca0,0x100f9100,0x10116a40}

    def hook(self, uc, address, size, user):
        sp=uc.reg_read(UC_X86_REG_ESP)
        arg=lambda n:self.read(sp+4+n*4)
        if address in self.edges:
            if address==0x1011cf70:
                self.event(5,*self.vector(arg(0)),*self.vector(arg(1)),arg(2),arg(3),arg(4));self.ret(0)
            elif address==0x10119320:
                # Controlled direction backend; original input angles recorded.
                self.event(30,*self.vector(arg(0)))
                for off,values in ((0x28,(1,0,0)),(0x34,(0,0,1)),(0x40,(0,1,0))):
                    for i,v in enumerate(values):self.write(self.globals+off+4*i,bits(v))
                self.ret(0)
            elif address==0x100ea240: self.ret(8)
            elif address==0x100f91b0:self.event(8,arg(0),arg(1),arg(2));self.ret(12)
            elif address==0x10119de0:self.event(9,arg(1),arg(2),arg(3),arg(4));self.ret(0)
            elif address==0x100f2a50:self.ret(4)
            elif address==0x100dfea0:
                for i,v in enumerate(self.vector(self.globals+0x28)):self.write(arg(0)+4*i,v)
                self.ret(8,arg(0))
            elif address==0x10088ca0:
                self.event(31,*[arg(i) for i in range(2,12)],arg(14),arg(16));self.ret(68,arg(0))
            elif address==0x100f9100:self.event(12,arg(0));self.ret(4)
            elif address==0x10116a40:self.event(11);self.ret(0)
            return
        if BASE<=address<BASE+0x300000:return super().hook(uc,address,size,user)
        if address==EDGE+0x7198:self.event(3,arg(0));self.ret(12)
        elif address==EDGE+0x7190:self.event(4);self.ret(0,1)
        elif address==EDGE+0x71c0:self.ret(0) # reserve=0 original Reload rejects
        elif address==EDGE+0x6228:
            for i,v in enumerate((2,3,4)):self.write(arg(0)+4*i,bits(v))
            self.ret(4,arg(0))
        elif address==EDGE+0x6104:
            for i,v in enumerate(self.vector(self.pev(1)+8)):self.write(arg(0)+4*i,v)
            self.ret(4,arg(0))
        elif address==EDGE+0x168:self.ret(0,2)
        elif address==EDGE+0xb8:self.event(6);self.ret(0)
        elif address in (EDGE+0xbc,EDGE+0xc0,EDGE+0xc8,EDGE+0xcc,EDGE+0xd0,EDGE+0xd4,EDGE+0xd8,EDGE+0xdc):self.ret(0)
        elif address==EDGE+0x11c:self.ret(0,4)
        elif address==EDGE+0x1ec:self.event(7,*self.vector(arg(4)),*self.vector(arg(5)),arg(8),arg(0));self.ret(0)
        else:raise ValueError(f'Unexpected Glock edge {address:08x}')

    def run(self,row):
        now,clip,primary,action,pressed,water,empty,aim,infinite,flags,scalar,deadline=row
        self.events=[];self.last=[]
        self.write(self.globals,now)
        for off,value in ((0x1cc,primary),(0x1d0,0),(0x1d8,0),(0x1b0,0),(0x128,action),
                          (0x140,0),(0x1e4,clip),(0x1b8,empty),(0x178,aim),(0x19c,scalar),(0x1a0,deadline)):
            self.write(self.weapon+off,value)
        self.write(self.obj(1)+0x22b0,pressed);self.write(self.obj(1)+0x21e8,infinite<<16)
        self.write(self.obj(1)+0x2814,0);self.write(self.pev(1)+0x1c4,water)
        self.write(self.obj(1)+0x2298,0);self.write(self.obj(1)+0x22a0,0)
        self.write(self.pev(1)+0x1a8,flags);self.write(self.obj(1)+0x2288,17)
        self.write(0x10226850,bits(37))
        for off,values in ((0x28,(1,2,3)),(0x34,(4,5,6)),(0x40,(7,8,9))):
            for i,v in enumerate(values):self.write(self.globals+off+4*i,bits(v))
        for obj in (1,4):
            for off,values in ((8,(11,12,13)),(0x178,(1,2,3)),(0x50,(4,5,6)),(0x74,(1,2,3)),(0x68,(.1,.2,.3))):
                for i,v in enumerate(values):self.write(self.pev(obj)+off+i*4,bits(v))
        for off in (0x18c,0x190,0x194):self.write(self.weapon+off,bits(.01))
        self.uc.reg_write(UC_X86_REG_ESP,STACK+0xff00)
        self.write(STACK+0xff00,EDGE+0xff00)
        self.uc.reg_write(UC_X86_REG_ECX,self.weapon)
        self.uc.reg_write(UC_X86_REG_MXCSR,0x1f80)
        self.uc.emu_start(0x10046dc0,EDGE+0xff00,count=20000)
        final=[self.read(self.weapon+off) for off in (0x1e4,0x1cc,0x1d0,0x1d8,0x1b0,0x19c,0x1a0)]
        final += [self.read(self.obj(1)+off) for off in (0x2298,0x22a0)]
        return final+self.events

def generate(sample,cases,expected):
    original=Glock(sample)
    rng=random.Random(319)
    for i in range(160):
        now=bits(rng.choice((.1,1.1,13.37,4096.125,8192.3)))
        row=[now,rng.randrange(1,16),0,0,1,0,0,rng.randrange(2),rng.randrange(2),rng.choice((0,0x4000)),bits(rng.random()*3),0]
        if i%10==0:row[2]=bits(number(now)+1)
        elif i%10==1:row[3]=1
        elif i%10==2:row[4]=0
        elif i%10==3:row[5]=3
        elif i%10==4:row[1]=0;row[6]=1
        elif i%10==5:row[1]=0
        result=original.run(row)
        cases.write(' '.join(f'{v:08x}' for v in row)+'\n')
        expected.write(' '.join(f'{v:08x}' for v in result)+'\n')
    return dict(sampleSha256=shared.SHA,originalFunction='10046DC0',cases=160,
                boundary='Full PrimaryAttack instructions; previously verified ballistics, animation, recoil, audio and engine consumers are controlled edges')
