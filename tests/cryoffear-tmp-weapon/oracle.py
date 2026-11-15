"""Execute the full original TMP PrimaryAttack; verified helpers are ABI edges."""
import importlib.util
from pathlib import Path
import random
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_MXCSR

spec=importlib.util.spec_from_file_location('automatic_primary_shared',Path(__file__).parents[1]/'cryoffear-p345-weapon/oracle.py')
shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)
BASE,WORK,STACK,EDGE=shared.BASE,shared.WORK,shared.STACK,shared.EDGE
bits,number=shared.bits,shared.number


class TMP(shared.P345):
    def __init__(self,sample):
        super().__init__(sample)
        self.write(self.weapon+0x168,14);self.write(self.weapon+0x198,bits(.25));self.write(self.weapon+0x1FC,31)
        self.write(0x10222EE4,WORK+0xb0000);self.write(WORK+0xb0000,WORK+0xb1000)
        self.write(WORK+0xb1000+0x20,EDGE+0x7020)
        self.write(0x10226660,91);self.write(0x10226490,92)
        self.edges.add(0x100FC9F0)

    def hook(self,uc,address,size,user):
        sp=uc.reg_read(UC_X86_REG_ESP);arg=lambda n:self.read(sp+4+n*4)
        if address==0x100FC9F0:
            self.event(20,0,arg(1),arg(5),arg(6),arg(3),arg(4));self.ret(0);return
        if address==EDGE+0x7020:self.ret(0,1);return
        if address==EDGE+0x7190:self.event(4);self.ret(0,1);return
        if address==EDGE+0xb8:self.message=arg(1);self.ret(0);return
        if address==EDGE+0xc0:
            if self.message==91:self.event(19,arg(0))
            elif self.message==92:self.event(6)
            self.ret(0);return
        return super().hook(uc,address,size,user)

    def run(self,row):
        now,clip,primary,action,pressed,water,empty,aim,infinite,flags,scalar,deadline=row
        self.events=[];self.last=[];self.message=0;self.write(self.globals,now)
        for off,value in ((0x1CC,primary),(0x1D0,0),(0x1D8,0),(0x1B0,0),(0x128,action),
                          (0x140,0),(0x1E4,clip),(0x1B8,empty),(0x178,aim),(0x19C,scalar),(0x1A0,deadline),
                          (0x1A8,1),(0x1AC,2)):
            self.write(self.weapon+off,value)
        self.uc.mem_write(self.weapon+0x1A5,b'\0')
        self.write(self.obj(1)+0x22B0,pressed);self.write(self.obj(1)+0x21E8,infinite<<16)
        self.write(self.obj(1)+0x2814,0);self.write(self.pev(1)+0x1C4,water)
        self.write(self.obj(1)+0x2298,0);self.write(self.obj(1)+0x22A0,0)
        self.write(self.pev(1)+0x1A8,flags);self.write(self.obj(1)+0x2288,17)
        # Original TMP converts global/1.65 to int before the real bullet call.
        self.write(0x10226850,bits(61.05))
        for off,values in ((0x28,(1,2,3)),(0x34,(4,5,6)),(0x40,(7,8,9))):
            for i,v in enumerate(values):self.write(self.globals+off+4*i,bits(v))
        for obj in (1,4):
            for off,values in ((8,(11,12,13)),(0x178,(1,2,3)),(0x50,(4,5,6)),(0x74,(1,2,3)),(0x68,(.1,.2,.3))):
                for i,v in enumerate(values):self.write(self.pev(obj)+off+i*4,bits(v))
        for off in (0x18C,0x190,0x194):self.write(self.weapon+off,bits(.01))
        self.uc.reg_write(UC_X86_REG_ESP,STACK+0xff00);self.write(STACK+0xff00,EDGE+0xff00)
        self.uc.reg_write(UC_X86_REG_ECX,self.weapon);self.uc.reg_write(UC_X86_REG_MXCSR,0x1f80)
        self.uc.emu_start(0x1007F1A0,EDGE+0xff00,count=20000)
        assert self.uc.reg_read(UC_X86_REG_EIP)==EDGE+0xff00
        final=[self.read(self.weapon+off) for off in (0x1E4,0x1CC,0x1D0,0x1D8,0x1B0,0x19C,0x1A0)]
        final += [self.read(self.obj(1)+off) for off in (0x2298,0x22A0)]
        return final+self.events


def generate(sample,cases,expected):
    original=TMP(sample);rng=random.Random(140)
    for i in range(320):
        now=bits(rng.choice((.1,1.1,13.37,4096.125,8192.3)))
        row=[now,rng.randrange(1,31),0,0,rng.randrange(2),0,0,rng.randrange(2),rng.randrange(2),rng.choice((0,0x4000)),bits(rng.random()*3),0]
        if i%10==0:row[2]=bits(number(now)+1)
        elif i%10==1:row[3]=1
        elif i%10==3:row[5]=3
        elif i%10==4:row[1]=0;row[6]=1
        elif i%10==5:row[1]=0
        cases.write(' '.join(f'{v:08x}'for v in row)+'\n')
        expected.write(' '.join(f'{v:08x}'for v in original.run(row))+'\n')
    return dict(sampleSha256=shared.shared.SHA,originalFunction='1007F1A0',cases=320,
                boundary='Full original PrimaryAttack; ballistics, direction, audio and effects are controlled ABI edges')
