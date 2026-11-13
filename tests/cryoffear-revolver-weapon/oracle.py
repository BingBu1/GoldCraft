# SPDX-License-Identifier: GPL-3.0-or-later
"""Original Revolver instructions with explicit preverified component edges."""
import importlib.util
from pathlib import Path
import random
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESI, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_MXCSR
spec=importlib.util.spec_from_file_location('cof_bullet_fixture',Path(__file__).parents[1]/'cryoffear-ballistics/oracle.py')
shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)
BASE,WORK,STACK,EDGE=shared.BASE,shared.WORK,shared.STACK,shared.EDGE
bits,number=shared.bits,shared.number

class Revolver(shared.Original):
    functions=(0x10064c60,0x10065270,0x10065430,0x10065660,0x10065760,0x10064320,0x10065ce0,0x10063d10,0x10063bc0,0x100efb41,0x100656a0)
    def __init__(self,sample):
        super().__init__(sample)
        self.write(self.weapon,WORK+0xa0000);self.write(self.weapon+4,self.pev(4))
        slots={8:0x1011f680,0x198:0x1011f5d0,0x1a4:0x10064c60,0x1a8:0x100656a0,
               0x1ac:0x10065c10,0x1c0:0x10065270,0x130:0x10063de0,0x12c:0x10064070,
               0x1d4:0x1001e060,0x1bc:0x1001e0c0,0x1b8:0x1001e060}
        for off,value in slots.items():self.write(WORK+0xa0000+off,value)
        for off in (0x190,0x1d0,0x1cc,0x168):self.write(WORK+0xa0000+off,EDGE+0x7000+off)
        for off in (8,0x104,0x228):self.write(self.vtable(1)+off,EDGE+0x6000+off)
        for off in (0xb8,0xbc,0xc0,0xc8,0xcc,0xd0,0xd4,0xd8,0xdc,0x168,0x11c,0x1ec,0x1c4,0xe4,0x128):self.write(0x10222f48+off,EDGE+off)
        for address,value in ((0x10226490,101),(0x10226668,102),(0x102265cc,103),(0x102265b8,104),(0x102264e8,105),(0x10231f50,35),(0x10231f54,36)):
            self.write(address,value)
        self.uc.mem_write(EDGE+0x7600,bytes.fromhex('d905')+(WORK+0xcd000).to_bytes(4,'little')+b'\xc3')
        self.write(WORK+0xcc000,0);self.uc.mem_write(WORK+0xcc100,b'weapon_revolver\0')
        self.edges={0x10119320,0x100ea240,0x100f91b0,0x10119de0,0x100f2a50,0x100dfea0,
                    0x10088ca0,0x100f9100,0x10116a40,0x100f5eb0,0x100f5da0,0x100e4ed0,
                    0x100dff70,0x1000f390,0x1011c1a0,0x100e0310}

    def hook(self,uc,address,size,user):
        sp=uc.reg_read(UC_X86_REG_ESP);arg=lambda n:self.read(sp+4+n*4)
        if EDGE+0x7600<=address<EDGE+0x7607:return
        if address in self.edges:
            if address==0x10119320:
                self.event(30,*self.vector(arg(0)))
                for off,values in ((0x28,(1,0,0)),(0x34,(0,0,1)),(0x40,(0,1,0))):
                    for i,v in enumerate(values):self.write(self.globals+off+4*i,bits(v))
                self.ret(0)
            elif address==0x100ea240:
                label=bytes(self.uc.mem_read(arg(0),32)).split(b'\0')[0]
                stages={b'revolver_reload_start':0,b'revolver_reload_insert':1,b'revolver_reload_end':2}
                if label in stages:self.event(20,stages[label])
                self.ret(8)
            elif address==0x100f91b0:self.event(8,arg(0),arg(1),arg(2));self.ret(12)
            elif address==0x10119de0:self.event(9,arg(1),arg(2),arg(3),arg(4));self.ret(0)
            elif address==0x100f2a50:self.ret(4)
            elif address==0x100dfea0:
                for i,v in enumerate(self.vector(self.globals+0x28)):self.write(arg(0)+4*i,v)
                self.ret(8,arg(0))
            elif address==0x10088ca0:self.event(31,*[arg(i) for i in range(2,12)],arg(14),arg(16));self.ret(68,arg(0))
            elif address==0x100f9100:self.event(12,arg(0));self.ret(4)
            elif address==0x10116a40:self.event(11);self.ret(0)
            elif address==0x100f5eb0:self.event(2);self.ret(0)
            elif address==0x100f5da0:self.ret(0,self.row[26])
            elif address==0x100e4ed0:self.ret(0,0)
            elif address==0x100dff70:self.ret(8,WORK+0xcc000)
            elif address==0x1000f390:
                self.write(WORK+0xcd000,bits(.25+arg(1)*.03125));uc.reg_write(UC_X86_REG_EIP,EDGE+0x7600)
            elif address==0x100e0310:self.ret(4,self.weapon)
            elif address==0x1011c1a0:
                # Previously verified common deploy is an explicit boundary.
                self.write(self.weapon+0x178,0);self.write(self.weapon+0x19c,0);self.write(self.weapon+0x1a0,self.row[1])
                if self.row[3]!=15:self.event(1,self.row[3])
                self.event(2);self.view(1)
                deadline=bits(number(self.row[1])+.28125)
                for off in (0x1cc,0x1d0,0x1d8):self.write(self.weapon+off,deadline)
                self.write(self.obj(1)+0x308,deadline);self.ret(24,1)
            return
        if address==0x100efc5d and self.mode==9:uc.emu_stop();return
        if BASE<=address<BASE+0x300000:return super().hook(uc,address,size,user)
        if address==EDGE+0x7190:self.event(4);self.ret(0,1)
        elif address in (EDGE+0x71d0,EDGE+0x71cc):self.ret(0,0)
        elif address==EDGE+0x7168:self.ret(0,1)
        elif address==EDGE+0x6228:
            for i,v in enumerate((2,3,4)):self.write(arg(0)+4*i,bits(v))
            self.ret(4,arg(0))
        elif address==EDGE+0x6104:
            for i,v in enumerate(self.vector(self.pev(1)+8)):self.write(arg(0)+4*i,v)
            self.ret(4,arg(0))
        elif address==EDGE+0x6008:self.write(self.obj(1)+0x4f8,bits(number(self.row[1])+number(arg(0))));self.ret(4)
        elif address==EDGE+0x168:self.ret(0,2)
        elif address==EDGE+0xe4:
            enabled=self.row[29];self.write(WORK+0xcd000,bits(enabled));uc.reg_write(UC_X86_REG_EIP,EDGE+0x7600)
        elif address==EDGE+0xb8:self.message=arg(1);self.packet=[];self.ret(0)
        elif address in (EDGE+0xc0,EDGE+0xc8,EDGE+0xcc,EDGE+0xd0,EDGE+0xd4,EDGE+0xd8):self.packet.append(arg(0));self.ret(0)
        elif address==EDGE+0xbc:
            p=self.packet
            if self.message==101:self.event(6)
            elif self.message==35:self.event(3,p[0])
            elif self.message==102:self.event(10,*p[:5])
            elif self.message==104:self.event(1,p[-1])
            elif self.message==105:self.event(21,*p)
            self.ret(0)
        elif address==EDGE+0x11c:self.ret(0,4)
        elif address==EDGE+0x128:self.ret(0,WORK+0xce000)
        elif address==EDGE+0x1ec:
            if arg(2)==35:self.event(7,*self.vector(arg(4)),arg(8))
            else:self.event(5,arg(3),arg(8),*self.vector(arg(4)))
            self.ret(0)
        elif address==EDGE+0xdc:self.ret(0,WORK+0xce000)
        else:raise ValueError(f'Unexpected Revolver edge {address:08x}')

    def view(self,sequence):
        self.write(self.pev(1)+0x21c,sequence);self.write(self.obj(1)+0x418,sequence);self.event(3,sequence)

    def run(self,row):
        self.row=row;self.mode=row[0];self.events=[];self.last=[];self.packet=[];self.message=0
        self.uc.mem_write(self.weapon+8,b'\0'*0x600);self.uc.mem_write(self.obj(1)+8,b'\0'*0x3000)
        self.uc.mem_write(self.pev(4),b'\0'*0x400)
        self.write(self.obj(1)+4,self.pev(1));self.write(self.obj(1)+0x252c,self.weapon)
        self.write(self.weapon+0x160,self.obj(1));self.write(self.weapon+0x168,21);self.write(self.weapon+0x1dc,1)
        for off,index in ((0x1e4,2),(0x1cc,4),(0x1d0,5),(0x1d4,6),(0x1d8,7),(0x128,9),(0x178,10),(0x1a4,11),(0x1fc,12),(0x1fd,13),(0x1fe,14),(0x200,15),(0x1f8,16)):
            if off in (0x178,0x1a4,0x1fc,0x1fd,0x1fe):self.uc.mem_write(self.weapon+off,bytes([row[index]]))
            else:self.write(self.weapon+off,row[index])
        self.write(self.weapon+0x120,15);self.write(self.weapon+0x198,bits(3.15));self.write(self.weapon+0x19c,row[30]);self.write(self.weapon+0x1a0,row[31]);self.write(self.weapon+0x1b8,row[24])
        self.write(self.obj(1)+0x2544,row[3]);self.write(self.obj(1)+0x308,row[8]);self.write(self.obj(1)+0x22b0,row[20]);self.write(self.obj(1)+0x22b4,row[21]);self.write(self.obj(1)+0x21e8,row[19]<<16)
        self.uc.mem_write(self.obj(1)+0x1ee8,bytes([row[27]]));self.write(self.obj(1)+0x1ee4,row[28]);self.write(self.obj(1)+0x2288,17)
        self.uc.mem_write(self.obj(1)+0x3f4+21,bytes([row[32]]));self.write(self.obj(1)+0x4d0,row[34])
        self.write(self.globals,row[1]);self.write(self.pev(4)+0x114,row[17]);self.write(self.pev(1)+0x21c,row[18]);self.write(self.pev(1)+0x184,row[22]);self.write(self.pev(1)+0x1c4,row[23]);self.write(self.pev(1)+0x1a8,row[25]);self.write(self.pev(1)+0x294,row[33]);self.write(self.pev(1)+0x260,0)
        self.write(self.pev(1)+0x274,bits(90));self.write(self.obj(1)+0x4f4,0);self.write(self.pev(4)+0x250,bits(9))
        self.write(0x10226868,bits(37));self.write(0x102318a4+21*44,5);self.write(0x10231890+21*44,1)
        self.write(self.globals+0x98,WORK+0xb0000);self.write(self.edict(4)+0x80,0x1c100)
        self.write(self.pev(4)+0x20c,self.edict(4));self.write(self.pev(1)+0x20c,self.edict(1))
        for off,values in ((0x28,(1,2,3)),(0x34,(4,5,6)),(0x40,(7,8,9))):
            for i,v in enumerate(values):self.write(self.globals+off+4*i,bits(v))
        for obj in (1,4):
            for off,values in ((8,(11,12,13)),(0x20,(4,5,6)),(0x178,(1,2,3)),(0x50,(4,5,6)),(0x74,(1,2,3)),(0x68,(.1,.2,.3))):
                for i,v in enumerate(values):self.write(self.pev(obj)+off+i*4,bits(v))
        # Host fixture inputs use native v_angle+punchangle in that order.
        for i,v in enumerate((1,2,3)):self.write(self.pev(1)+0x74+i*4,bits(v))
        for i,v in enumerate((.1,.2,.3)):self.write(self.pev(1)+0x68+i*4,bits(v))
        for off in (0x18c,0x190,0x194):self.write(self.weapon+off,bits(.01))
        for off,v in ((0x17c,1),(0x180,2),(0x184,3)):self.write(self.weapon+off,bits(v))
        self.write(self.weapon+0x188,55)
        self.uc.reg_write(UC_X86_REG_ESP,STACK+0xff00);self.write(STACK+0xff00,EDGE+0xff00)
        self.uc.reg_write(UC_X86_REG_ECX,self.weapon);self.uc.reg_write(UC_X86_REG_ESI,self.obj(1));self.uc.reg_write(UC_X86_REG_MXCSR,0x1f80)
        try:self.uc.emu_start(self.functions[self.mode],0x100efc5d if self.mode==9 else EDGE+0xff00,count=40000)
        except Exception as error:raise RuntimeError(f'Original mode={self.mode} row={row} EIP={self.uc.reg_read(UC_X86_REG_EIP):08x} last={[hex(v) for v in self.last]} events={self.events}') from error
        if self.uc.reg_read(UC_X86_REG_EIP)!=(0x100efc5d if self.mode==9 else EDGE+0xff00):raise ValueError('Original failed to return')
        think=self.read(self.weapon+0x78);think_id={0:0,0x10065660:1,0x10065430:2,0x10065760:3}.get(think,-1)
        final=[self.read(self.weapon+off) for off in (0x1e4,)]+[self.read(self.obj(1)+0x2544)]
        final += [self.read(self.weapon+off) for off in (0x1cc,0x1d0,0x1d4,0x1d8)]+[self.read(self.obj(1)+0x308)]
        final += [self.read(self.weapon+0x128),self.read(self.weapon+0x178)&255,self.read(self.weapon+0x1a4)&255]
        final += [self.read(self.weapon+off)&255 for off in (0x1fc,0x1fd,0x1fe)]
        final += [self.read(self.weapon+off) for off in (0x200,0x1f8)]+[self.read(self.pev(4)+0x114),self.read(self.pev(1)+0x21c)]
        final += [self.read(self.obj(1)+off) for off in (0x2298,0x22a0)]+[self.read(self.weapon+off) for off in (0x19c,0x1a0)]
        final += [0,self.read(self.obj(1)+0x4f8) if self.read(self.obj(1)+0x78) else self.read(self.pev(4)+0x104),self.read(self.pev(1)+0x294),self.read(self.obj(1)+0x4d0),self.read(self.obj(1)+0x4f4),self.read(self.pev(4)+0x250)]
        return final+self.events

def generate(sample,cases,expected):
    original=Revolver(sample);rng=random.Random(898);count=0;by_mode={}
    def row(mode,now=13.37):return [mode,bits(now),3,10,0,0,0,0,0,0,0,0,0,0,0,0,216,3,0,0,0,0,0,0,0,0,0,0,0,1,bits(.7),bits(10000),1,bits(216),0]
    def emit(r):
        nonlocal count
        result=original.run(r);cases.write(' '.join(f'{v:08x}' for v in r)+'\n');expected.write(' '.join(f'{v&0xffffffff:08x}' for v in result)+'\n');count+=1;by_mode[r[0]]=by_mode.get(r[0],0)+1
    for mode in range(11):
        for i in range(72):
            r=row(mode,rng.choice((.1,1.1,13.37,4096.125,8192.3)));r[2]=rng.randrange(6);r[3]=rng.randrange(16);r[17]=rng.randrange(6);r[10]=rng.randrange(2);r[19]=rng.randrange(2)
            if mode==0:
                r[20]=1;r[3]=0
                if i%8==0:r[4]=bits(number(r[1])+1)
                elif i%8==1:r[9]=1
                elif i%8==2:r[20]=0
                elif i%8==3:r[23]=3
                elif i%8==4:r[2]=0;r[24]=1
                elif i%8==5:r[2]=0
                elif i%8==6:r[12]=1
                r[25]=0x4000 if i%2 else 0
            elif mode==1:
                r[9]=(0,1,2)[i%3];r[11]=int(i%7==0);r[12]=int(i%11==0)
                if i%13==0:r[4]=bits(number(r[1])+1)
            elif mode==2:r[12]=1;r[13]=i%2;r[9]=2
            elif mode in (3,4):pass
            elif mode==5:
                r[10]=0;r[9]=(0,1,2,5)[i%4];r[26]=int(i%5==0);r[25]=0x40000 if i%3==0 else 0
                r[12]=int(i%7==0);r[13]=0;r[14]=int(i%3==0);r[15]=r[1]+(-1,0,1)[i%3]
                r[20]=int(i%7==0);r[18]=(0,12,13,14)[i%4]
                if i%12==0:r[11]=1;r[10]=1;r[9]=0
                elif i%12==1:r[22]=0x40;r[20]=0x40;r[9]=0
                elif i%12==2:r[22]=0x2000;r[9]=0
                elif i%12==3:r[22]=0x800;r[9]=0
                elif i%12==4:r[22]=1;r[20]=1;r[9]=0
            elif mode==6:r[9]=(0,1,2)[i%3];r[7]=r[1]+(-1,0,1)[i%3]
            elif mode==7:pass
            elif mode==8:r[32]=i%2;r[10]=0;r[12]=i%2;r[13]=i%2
            elif mode==9:
                r[33]=bits(rng.choice((0,1,12,216,348,360)));r[16]=rng.choice((0,72,216,360));r[34]=r[1]+(-1,0,1)[i%3];r[30]=0
            elif mode==10:r[9]=(0,1,2)[i%3]
            emit(r)
    required=(0x10064cdf,0x10064d74,0x10064de2,0x10064e0a,0x10065303,0x10065340,
              0x10065449,0x100654cd,0x10065500,0x10065515,0x1006552a,0x1006553f,
              0x10065554,0x100655d0,0x1006566f,0x1006576f,0x1006434e,0x1006473b,
              0x100647d0,0x10064885,0x10064993,0x10064a31,0x10064af6,0x10065d24)
    if not all(v in original.visited for v in required):raise ValueError(f'Missing original branches {[hex(v) for v in required if v not in original.visited]}')
    return dict(sampleSha256=shared.SHA,cases=count,requiredInstructions=[f'{v:08x}' for v in required],byMode=by_mode,functions=[f'{v:08x}' for v in original.functions],visitedInstructions=len(original.visited),boundary='Full Revolver methods, common Post and actual view-animation instructions; ballistics, player animation/audio/metadata/model-duration consumers are controlled verified edges. DefaultDeploy helper is a declared controlled composition boundary; owner cylinder fragment runs exact bytes through EFC5D.')
