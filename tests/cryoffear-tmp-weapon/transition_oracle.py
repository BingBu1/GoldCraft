"""Run full original TMP derived Post, aim toggle and Holster functions.

Common Post, metadata lookup, model timings and sound helpers are explicit
boundaries. Original normal and special item behavior are not conflated.
"""
import importlib.util
from pathlib import Path
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_MXCSR

spec=importlib.util.spec_from_file_location('tmp_primary_transition',Path(__file__).with_name('oracle.py'))
shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)
WORK,STACK,EDGE,bits=shared.WORK,shared.STACK,shared.EDGE,shared.bits


class Transitions(shared.TMP):
    def __init__(self,sample):
        super().__init__(sample)
        self.write(WORK+0xa0000+8,0x1011F680)
        self.write(WORK+0xa0000+0x12C,0x1007E5B0);self.write(WORK+0xa0000+0x130,0x1007E2E0)
        self.write(0x10223070,EDGE+0x128);self.write(0x1022302C,EDGE+0x7480)
        self.uc.mem_write(EDGE+0x7480,bytes.fromhex('d905')+(WORK+0xce000).to_bytes(4,'little')+b'\xc3')
        self.write(WORK+0xce000,bits(1));self.model=WORK+0xc0000
        self.write(self.model+164,16);self.write(self.model+168,244)
        for i in range(16):
            self.write(self.model+244+i*176+32,bits(30));self.write(self.model+244+i*176+56,31)

    def hook(self,uc,address,size,user):
        sp=uc.reg_read(UC_X86_REG_ESP);arg=lambda n:self.read(sp+4+n*4)
        if address==0x100DFF70:self.ret(8,WORK+0xf0000);return
        if address==EDGE+0xb8:
            if not self.aimMessages:self.event(21,int(self.read(self.weapon+0x178)!=0))
            self.aimMessages+=1;self.ret(0);return
        if address in (EDGE+0xbc,EDGE+0xc0,EDGE+0xc8,EDGE+0xcc,EDGE+0xd0,EDGE+0xd4,EDGE+0xd8,EDGE+0xdc):
            self.ret(0);return
        if address==0x1011D560:self.ret(0);return
        if address==0x100F5DA0:self.ret(0,self.condition);return
        if address==EDGE+0x128:self.ret(0,self.model);return
        if EDGE+0x7480<=address<=EDGE+0x7486:return
        if address==EDGE+0x7198:
            self.event(3,arg(0));self.write(self.pev(1)+0x21C,arg(0));self.ret(12);return
        return super().hook(uc,address,size,user)

    def run(self,row):
        op,now,clip,action,aim,flags,speed,self.condition,primary,secondary,extra,owner,pending,gate,gateTime=row
        self.events=[];self.last=[];self.aimMessages=0
        self.uc.mem_write(self.weapon+8,b'\0'*0x600)
        self.write(self.weapon+0x188,55);self.write(self.weapon+0x160,self.obj(1));self.write(self.weapon+0x168,14)
        self.write(self.globals,now)
        for off,value in ((0x1E4,clip),(0x128,action),(0x178,aim),(0x1CC,primary),(0x1D0,secondary),
                          (0x1D4,extra),(0x1D8,0),(0x1A4,pending)):
            self.write(self.weapon+off,value)
        for off,value in ((0x308,owner),(0x1EE8,gate),(0x1EE4,gateTime)):
            self.write(self.obj(1)+off,value)
        self.write(self.obj(1)+0x22B0,0x40 if op in (1,2) else 0);self.write(self.obj(1)+0x22B4,0)
        self.write(self.pev(1)+0x184,0);self.write(self.pev(1)+0x20,speed);self.write(self.pev(1)+0x24,0)
        self.write(self.pev(1)+0x1A8,flags)
        self.uc.reg_write(UC_X86_REG_ESP,STACK+0xff00);self.write(STACK+0xff00,EDGE+0xff00)
        self.uc.reg_write(UC_X86_REG_ECX,self.weapon);self.uc.reg_write(UC_X86_REG_MXCSR,0x1F80)
        self.uc.emu_start({0:0x1007E8A0,1:0x10080030,2:0x10080030,3:0x1007E2A0}[op],EDGE+0xff00,count=15000)
        return [*[self.read(self.weapon+off)for off in (0x128,0x178,0x1CC,0x1D0,0x1D4,0x1D8)],
                self.read(self.obj(1)+0x308),self.read(self.weapon+0x1A4),*self.events]


def generate(sample,cases,expected):
    original=Transitions(sample);count=0
    for now in (.1,13.37,8192.3):
        for clip in (0,1,30):
            rows=[[op,bits(now),clip,0,op==2,0,0,0,0,0,0,0,0,0,0]for op in range(4)]
            rows += [[0,bits(now),clip,action,0,flags,bits(speed),condition,0,0,0,0,0,0,0]
                     for action,flags,speed,condition in ((0,0x40000,10,0),(1,0x40000,10,0),
                         (1,0,0,0),(0,0,0,1),(5,0,0,0),(2,0,0,0))]
            for row in rows:
                cases.write(' '.join(f'{int(v):08x}'for v in row)+'\n')
                expected.write(' '.join(f'{v:08x}'for v in original.run(row))+'\n');count+=1
    return dict(cases=count,functions=['1007E8A0','1007E5B0','1007E2E0','10080030','1007E2A0'],
                boundary='Complete derived Post/aim/Holster; controlled common Post/metadata/model duration/condition; no special lifecycle')
