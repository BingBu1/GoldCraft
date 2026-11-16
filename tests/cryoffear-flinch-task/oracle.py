# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute original StartTask/RunTask case29 and the real classname helper."""
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('flinch_selector',ROOT/'tests/cryoffear-flinch-activity/oracle.py')
selector=importlib.util.module_from_spec(spec)
spec.loader.exec_module(selector)
shared=selector.shared
NAMES=('monster_normal','monster_bosschainsaw','monster_booksimon','monster_doctorboss',
       'monster_sewerboss','MONSTER_BOOKSIMON','monster_booksimon_extra','')
LOOKUPS=((3,3,3,3),(-1,-1,-1,-1),(3,-1,-1,-1),(-1,3,-1,7),
         (-2,0,1,2),(-1,-1,0,0),(3,4,5,6),(-1,-1,3,4))


class Task(selector.Flinch):
    def hook(self,uc,address,size,user):
        if shared.BASE<=address<shared.BASE+0x300000:self.visited.add(address)
        sp=uc.reg_read(selector.UC_X86_REG_ESP)
        if address==0x10119320:
            assert self.read(sp+4)==self.pev(1)+0x50
            self.events.append(1)
            assert self.vectors<2
            self.write(self.obj(1)+0x21c,self.groups[self.vectors]);self.vectors+=1
            self.ret(0)
        elif address==0x1000e950:
            assert uc.reg_read(selector.UC_X86_REG_ECX)==self.obj(1)
            self.events.extend((2,self.read(sp+4)))
            assert self.lookups<4
            value=LOOKUPS[self.mode][self.lookups];self.lookups+=1
            if self.mode>=6 and self.lookups==2:
                self.write(self.obj(1)+0x180,self.read(self.obj(1)+0x180)^0x40000000)
            self.ret(4,value)
        elif shared.EDGE<=address<shared.EDGE+0x10000 and address!=shared.EDGE+0xff00:
            raise ValueError(f'Unexpected original callback {address:08x}')

    def snapshot(self):return [self.read(self.obj(1)+o) for o in (0x180,0x22c,0x218,0xf8)]

    def call(self,entry):
        task=shared.WORK+0xa1000;self.write(task,29)
        self.write(shared.STACK+0xff00,shared.EDGE+0xff00);self.write(shared.STACK+0xff04,task)
        self.uc.reg_write(selector.UC_X86_REG_ESP,shared.STACK+0xff00)
        self.uc.reg_write(selector.UC_X86_REG_ECX,self.obj(1))
        self.uc.emu_start(entry,shared.EDGE+0xff00,count=4000)
        assert self.uc.reg_read(selector.UC_X86_REG_EIP)==shared.EDGE+0xff00

    def run(self,row):
        name,skin,conditions,status,ideal,finished,g1,g2,self.mode,run_conditions=row
        self.uc.mem_write(shared.WORK+0xb0010,NAMES[name].encode()+b'\0')
        self.write(self.pev(1),16);self.write(self.pev(1)+0x110,skin)
        for offset,value in ((0x180,conditions),(0x22c,status),(0x218,ideal),(0xf8,finished)):
            self.write(self.obj(1)+offset,value)
        self.groups=(g1,g2);self.events=[];self.vectors=self.lookups=0
        self.call(0x1000bc60)
        before=[*self.snapshot(),len(self.events),*self.events]
        self.write(self.obj(1)+0x180,run_conditions)
        self.call(0x1000b2f0)
        return [*before,*self.snapshot()]


def generate(sample,proof,cases,expected):
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    pre=json.loads(proof.read_text('utf8'))
    if not pre['beforeImplementation'] or digest(sample)!=pre['sampleSha256'] or digest(sample)!=shared.SHA:
        raise ValueError('Original identity/pre-edit proof differs')
    spec=importlib.util.spec_from_file_location('task_mapper',ROOT/'tests/cryoffear-magazine-actions/run.py')
    mapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(mapper)
    image=mapper.mapped_image(sample.read_bytes())
    for region in pre['regions']:
        data=bytes.fromhex(region['hex']);start=int(region['va'],16)-shared.BASE
        if image[start:start+len(data)]!=data or hashlib.sha256(data).hexdigest()!=region['sha256']:
            raise ValueError('Original IDA region mismatch')
    for name,sha in pre['archives'].items():
        if digest(proof.parent/name)!=sha:raise ValueError('Original archive mismatch')
    machine=Task(sample);count=0;groups=(0,1,3,4,5,6,7,19)
    for name in range(len(NAMES)):
        for skin in (-1,0,1,2):
            for failed in (0,0x40000000):
                for mode in range(len(LOOKUPS)):
                    conditions=failed|0x100;run_conditions=conditions^(0x40000000 if count%2 else 0)
                    row=[name,skin&0xffffffff,conditions,(1,4,9)[count%3],777,
                         (0,1,0xffffffff)[count%3],groups[mode],groups[(mode+3)%8],mode,run_conditions]
                    cases.write(' '.join(f'{v:08x}' for v in row)+'\n')
                    expected.write(' '.join(f'{v:08x}' for v in machine.run(row))+'\n');count+=1
    covered=lambda at:any(int(r['va'],16)<=at<int(r['va'],16)+len(bytes.fromhex(r['hex'])) for r in pre['regions'])
    missing=[hex(at) for at in sorted(machine.visited) if at not in (0x10119320,0x1000e950) and not covered(at)]
    if missing:raise ValueError('Unpinned executed original instructions: '+','.join(missing))
    return dict(sampleSha256=shared.SHA,proofSha256=digest(proof),cases=count,
        visitedInstructions=len(machine.visited),uncoveredInstructions=missing,
        original=['StartTask1000BC60/case29','RunTask1000B2F0/case29','classname1000AAF0','selector10089C80'],
        boundary='Actual original task dispatch/class checks; controlled MakeVectors/LookupActivity; no live scheduler/target')
