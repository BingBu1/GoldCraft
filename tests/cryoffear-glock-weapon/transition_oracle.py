# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute complete original Glock derived Post, light callbacks and Holster.
The common base Post is an explicit edge; its independently verified ordinary
SDK dispatch is exercised separately. One-second model timings are controlled.
"""
from oracle import Glock, WORK, STACK, EDGE, bits
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_MXCSR, UC_X86_REG_EIP

class Transitions(Glock):
    def __init__(self,sample):
        super().__init__(sample)
        self.write(WORK+0xa0000+8,0x1011f680)
        self.write(WORK+0xa0000+0x19c,EDGE+0x7500)
        self.write(0x10223070,EDGE+0x128)
        self.write(0x1022302c,EDGE+0x7480)
        # Native x87 return of float cvar1, preserving original compare code.
        self.uc.mem_write(EDGE+0x7480,bytes.fromhex('d905')+(WORK+0xce000).to_bytes(4,'little')+bytes.fromhex('c3'))
        self.write(WORK+0xce000,bits(1))
        self.model=WORK+0xc0000
        self.write(self.model+164,43);self.write(self.model+168,244)
        for i in range(43):
            self.write(self.model+244+i*176+32,bits(30));self.write(self.model+244+i*176+56,31)
        self.write(0x10222ee4,0)

    def hook(self,uc,address,size,user):
        sp=uc.reg_read(UC_X86_REG_ESP);arg=lambda n:self.read(sp+4+n*4)
        if address==0x1011d560:uc.reg_write(UC_X86_REG_EIP,0x100459b0);return
        if address==0x100fc9f0:
            text=bytes(uc.mem_read(arg(2),80)).split(b'\0',1)[0]
            value=1 if b'rifle_insert' in text else 0 if b'generic2' in text else 2
            self.event(22,value);self.ret(0);return
        if address==0x100f3d90:self.event(21,1);self.ret(8);return
        if address==EDGE+0x168:self.ret(0,self.randomFirst if arg(1)==599 else self.randomSecond);return
        if address==EDGE+0x7500:self.event(18,arg(1));self.ret(8);return
        if address==EDGE+0x128:self.ret(0,self.model);return
        if EDGE+0x7480<=address<=EDGE+0x7486:return
        if address==EDGE+0x7198:
            self.event(3,arg(0));self.write(self.pev(1)+0x21c,arg(0));self.ret(12);return
        return super().hook(uc,address,size,user)

    def run(self,row):
        op,now,clip,action,aim,flags,speed,jump,primary,secondary,extra,owner,light,installed,pending,gate,gateTime,self.randomFirst,self.randomSecond=row
        self.events=[];self.last=[]
        self.uc.mem_write(self.weapon+8,b'\0'*0x600)
        self.write(self.weapon+0x160,self.obj(1));self.write(self.weapon+0x168,3)
        self.write(self.globals,now)
        for off,val in ((0x1e4,clip),(0x128,action),(0x178,aim),(0x1cc,primary),(0x1d0,secondary),(0x1d4,extra),(0x1d8,0),(0x1f8,pending),(0x200,installed)):
            self.write(self.weapon+off,val)
        for off,val in ((0x308,owner),(0x5d0,light*4),(0x3aa,jump),(0x1ee8,gate),(0x1ee4,gateTime)):
            self.write(self.obj(1)+off,val)
        self.write(self.pev(1)+0x20,speed);self.write(self.pev(1)+0x24,0)
        self.write(self.pev(1)+0x1a8,flags);self.write(self.pev(4)+0x114,0)
        self.uc.reg_write(UC_X86_REG_ESP,STACK+0xff00);self.write(STACK+0xff00,EDGE+0xff00)
        self.uc.reg_write(UC_X86_REG_ECX,self.weapon);self.uc.reg_write(UC_X86_REG_MXCSR,0x1f80)
        self.uc.emu_start({0:0x10046220,1:0x10046c30,2:0x100456c0,3:0x10047db0,4:0x10047cc0,5:0x10045b40}[op],EDGE+0xff00,count=15000)
        final=[self.read(self.weapon+off) for off in (0x128,0x178,0x1cc,0x1d0,0x1d4,0x1d8)]
        final += [self.read(self.obj(1)+0x308),self.read(self.pev(4)+0x114),int(bool(self.read(self.obj(1)+0x5d0)&4)),self.read(self.weapon+0x200)]
        return final+self.events

def generate(sample,cases,expected):
    original=Transitions(sample);count=0
    for now in (.1,13.37,8192.3):
        for clip in (0,1,15):
            for op in range(6):
                # Callback operations receive their engine-authenticated stage
                # from actual SDK setup; source oracle runs their actual body.
                row=[op,bits(now),clip,0,0,0,0,0,0,0,0,0,0,op in (3,4),op in (0,1,2),0,0,2,2]
                cases.write(' '.join(f'{int(v):08x}' for v in row)+'\n')
                expected.write(' '.join(f'{v:08x}' for v in original.run(row))+'\n');count+=1
            for action,flags,speed,jump in ((0,0x40000,10,0),(1,0x40000,10,0),(1,0,0,0),(0,0,0,1),(5,0,0,0)):
                row=[0,bits(now),clip,action,0,flags,bits(speed),jump,0,0,0,0,0,0,0,0,0,2,2]
                cases.write(' '.join(f'{int(v):08x}' for v in row)+'\n')
                expected.write(' '.join(f'{v:08x}' for v in original.run(row))+'\n');count+=1
    for clip in (0,15):
        for choice in (0,1,2):
            row=[0,bits(10),clip,0,0,0,0,0,0,0,0,0,0,0,0,0,0,50,choice]
            cases.write(' '.join(f'{int(v):08x}' for v in row)+'\n')
            expected.write(' '.join(f'{v:08x}' for v in original.run(row))+'\n');count+=1
    return dict(cases=count,functions=['10046220','10046C30','100456C0','10047DB0','10047CC0','10045B40'],boundary='Complete derived state instructions with controlled common-post/audio/tutorial/model engine edges; special suicide state excluded')
