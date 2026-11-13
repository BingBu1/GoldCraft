# SPDX-License-Identifier: GPL-3.0-or-later
"""Run original sound consumers and ordinary-WAV helper to real engine edge."""
import importlib.util
from pathlib import Path
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBX, UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_MXCSR

spec=importlib.util.spec_from_file_location('cof_sound_original',Path(__file__).parents[1]/'cryoffear-ballistics/oracle.py')
shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)
BASE,WORK,STACK,EDGE=shared.BASE,shared.WORK,shared.STACK,shared.EDGE
bits=shared.bits
SAMPLES={
 'weapons/weapon_fire_empty.wav':21,'weapons/weapon_fire_empty_noclick.wav':22,
 'weapons/sleeve_generic1.wav':10,'weapons/sleeve_generic2.wav':11,'weapons/sleeve_generic3.wav':12,
 'weapons/weapon_get.wav':9,'weapons/item_get.wav':8,'weapons/rifle/rifle_insert.wav':7}

class Original(shared.Original):
    def __init__(self,sample):
        super().__init__(sample)
        self.write(self.weapon+4,self.pev(4));self.write(self.weapon+0x160,self.obj(1))
        self.write(0x10222fbc,EDGE+0x74);self.write(0x102230b0,EDGE+0x168)

    def hook(self,uc,address,size,user):
        self.visited.add(address)
        sp=uc.reg_read(UC_X86_REG_ESP);arg=lambda n:self.read(sp+4+n*4)
        if address==0x1013dbb3:self.ret(0)
        elif address==0x1000aba0:self.ret(0,0) # actual Glock, never dualweapon.
        elif address==EDGE+0x168:
            # Engine RANDOM_LONG arguments are integer, not floats.
            bounds=(arg(0),arg(1));self.rng.append(bounds)
            self.ret(0,self.pitch-100 if len(self.rng)==1 else self.choice)
        elif address==EDGE+0x74:
            raw=bytes(self.uc.mem_read(arg(2),128)).split(b'\0')[0].decode('ascii')
            if raw not in SAMPLES:raise ValueError('Unknown original sound '+raw)
            self.calls += [SAMPLES[raw],self.index(arg(0),self.edict),arg(1),arg(3),arg(4),arg(5),arg(6)]
            self.ret(0)
        elif BASE<=address<BASE+0x300000:pass
        else:raise ValueError(f'Unexpected original edge {address:08x}')

    def run(self,row):
        kind,value,pitch,empty,quiet,suppressed,armor=row
        self.calls=[];self.rng=[];self.choice=value;self.pitch=pitch
        self.write(self.weapon+0x1b4,empty)
        self.write(self.pev(4)+0x1b4,armor)
        self.write(0x10226480,suppressed);self.write(0x10226489,quiet)
        start,end={4:(0x1011e4a0,EDGE+0xff00),16:(0x1011c3fe,0x1011c4bc),
                   14:(0x1011cc54,0x1011ccc6),17:(0x1011b737,0x1011b77a)}.get(kind,(0,0))
        if kind==22:start,end=((0x100464f0,0x10046522),(0x10046c30,0x10046c5e),(0x100456c0,0x100456ee))[value]
        self.uc.reg_write(UC_X86_REG_ECX,self.weapon);self.uc.reg_write(UC_X86_REG_ESI,self.weapon)
        self.uc.reg_write(UC_X86_REG_EDI,self.weapon if kind==16 else self.obj(1))
        self.uc.reg_write(UC_X86_REG_EBX,self.weapon)
        self.uc.reg_write(UC_X86_REG_ESP,STACK+0xff00);self.write(STACK+0xff00,EDGE+0xff00)
        self.uc.reg_write(UC_X86_REG_MXCSR,0x1f80)
        self.uc.emu_start(start,end,count=2000)
        if self.uc.reg_read(UC_X86_REG_EIP)!=end:raise ValueError('Original consumer truncated')
        return [self.read(self.weapon+0x1b4),self.read(self.pev(4)+0x1b4),len(self.rng),len(self.calls)//7,*self.calls]

def generate(sample,cases,expected):
    original=Original(sample);count=0
    for kind in (4,16,14,17,22):
        for quiet in (0,1):
            for armor in (bits(-2),bits(0),bits(.5),bits(1),bits(2),0x7fc00001):
                for value in (range(3) if kind in (16,22) else (0,)):
                    for pitch in ((95,100,105) if kind==16 else (100,)):
                        for empty in ((0,1,0xffffffff) if kind==4 else (1,)):
                            for suppressed in ((0,1) if kind==14 else (0,)):
                                row=[kind,value,pitch,empty,quiet,suppressed,armor]
                                result=original.run(row)
                                cases.write(' '.join(f'{v:08x}' for v in row)+'\n')
                                expected.write(' '.join(f'{v:08x}' for v in result)+'\n');count+=1
    mandatory=(0x1011e4cb,0x1011e4d2,0x1011e4e9,0x1011c4b2,0x1011c42a,0x100fca19,0x100fca2f,
               0x1011cc5b,0x1011cc92,0x1011b74d,0x1004651d,0x10046c59,0x100456e9)
    if not all(v in original.visited for v in mandatory):raise ValueError('Original branch coverage incomplete')
    return dict(sampleSha256=shared.SHA,cases=count,instructions=len(original.visited),mandatoryBranches=len(mandatory),
                boundary='Full original EmptyPlay plus accepted pickup/ammo/attachment/sleeve consumer windows and full ordinary WAV helper; actual RNG/string-classifier/engine EmitSound are controlled edges')
