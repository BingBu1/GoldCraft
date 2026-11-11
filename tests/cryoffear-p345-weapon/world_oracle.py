# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute original Spawn/FallThink/Materialize/Drop/Kill instructions."""
from oracle import shared, BASE, WORK, STACK, EDGE, bits
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_MXCSR


class World(shared.Original):
    def __init__(self, sample):
        super().__init__(sample)
        self.write(self.weapon, WORK+0xa0000)
        self.write(self.weapon+4, self.pev(4))
        self.write(WORK+0xa0000+8, 0x1011f680)
        self.write(WORK+0xa0000+0x30, EDGE+0x7000)
        self.write(0x10222f50, EDGE+0x7100)

    def hook(self, uc, address, size, user):
        sp=uc.reg_read(UC_X86_REG_ESP)
        arg=lambda n:self.read(sp+4+n*4)
        if address==0x10119fe0:
            self.event(102,*self.vector(arg(1)));self.ret(0);return
        if address==0x1011a030:
            self.event(103,*self.vector(arg(1)),*self.vector(arg(2)));self.ret(0);return
        if address==EDGE+0x7000:self.event(100);self.ret(0);return
        if address==EDGE+0x7100:self.event(101);self.ret(0);return
        return super().hook(uc,address,size,user)

    def call(self, address):
        self.uc.reg_write(UC_X86_REG_ESP,STACK+0xff00)
        self.write(STACK+0xff00,EDGE+0xff00)
        self.uc.reg_write(UC_X86_REG_ECX,self.weapon)
        self.uc.reg_write(UC_X86_REG_MXCSR,0x1f80)
        self.uc.emu_start(address,EDGE+0xff00,count=10000)

    def run(self,row):
        now,health,touch,ground,hidden,retire=row
        self.events=[];self.last=[]
        # Fresh engine-zeroed entity; keep only its vtable/pev.
        self.uc.mem_write(self.weapon+8,b'\0'*0x600)
        self.uc.mem_write(self.pev(4),b'\0'*0x400)
        self.write(self.pev(4)+0x20c,self.edict(4))
        self.write(self.globals,now)
        self.write(self.pev(4)+0x168,health)
        self.write(self.pev(4)+0x254,touch)
        for i,value in enumerate((11,12,13)):self.write(self.pev(4)+8+4*i,bits(value))
        self.call(0x1005f510)
        self.write(self.pev(4)+0x1a8,0x200 if ground else 0)
        self.write(self.pev(4)+0x118,0x80 if hidden else 0)
        self.write(self.pev(4)+0x50,bits(17));self.write(self.pev(4)+0x58,bits(19))
        self.call(0x1011d220)
        if retire:self.call(0x1011cf10 if retire==1 else 0x1011dfd0)
        think=self.read(self.weapon+0x78)
        final=[self.read(self.weapon+off) for off in (0x1f4,0x1e4)]
        final += [self.read(self.pev(4)+off) for off in (0x108,0x10c,0x160,0x50,0x58,0x118,0x104)]
        final += [int(self.read(self.weapon+0x80)!=0),int(self.read(self.weapon+0x7c)!=0),
                  1 if think==0x1011d220 else 2 if think==0x101028e0 else 0,
                  self.read(self.weapon+0x108)&8]
        return final+self.events


def generate(sample,cases,expected):
    original=World(sample);count=0
    for now in (.1,13.37,8192.3):
        for health in (-2,-1,0,.5,8,13.9):
            for touch in (0,1):
                for ground,hidden,retire in ((0,0,0),(1,0,0),(1,1,0),(1,1,1),(0,1,2)):
                    row=[bits(now),bits(health),touch,ground,hidden,retire]
                    values=original.run(row)
                    cases.write(' '.join(f'{v:08x}' for v in row)+'\n')
                    expected.write(' '.join(f'{v:08x}' for v in values)+'\n');count+=1
    return dict(sampleSha256=shared.SHA,cases=count,
                functions=['1005F510','1011D150','1011D220','1011E1C0','1011CF10','1011DFD0','1011F680'],
                boundary='Complete original world state instructions; controlled Precache, SetModel, SetOrigin and SetSize edges; no pickup acceptance')
