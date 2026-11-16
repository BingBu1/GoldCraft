# SPDX-License-Identifier: GPL-3.0-or-later
"""Run original model activity selection, preserving each native RNG call."""
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import struct

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('activity_shared',ROOT/'tests/cryoffear-ballistics/oracle.py')
shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)


class Activity(shared.Original):
    def __init__(self,sample):
        super().__init__(sample);self.write(0x102230b0,shared.EDGE+0x300)

    def hook(self,uc,address,size,user):
        if shared.BASE<=address<shared.BASE+0x300000:self.visited.add(address)
        if address==shared.EDGE+0x300:
            sp=uc.reg_read(shared.UC_X86_REG_ESP);low=self.read(sp+4);high=self.read(sp+8)
            assert low==0
            self.seed=(self.seed*1664525+1013904223)&0xffffffff
            value=self.seed%(high+1) if high<0x80000000 else self.seed
            self.events.extend((low,high,value));self.ret(0,value)
        elif shared.EDGE<=address<shared.EDGE+0x10000 and address!=shared.EDGE+0xff00:
            raise ValueError(f'Unexpected callback {address:x}')

    def run(self,query,seed,sequences):
        model=shared.WORK+0xa0000;self.write(model+0xa4,len(sequences));self.write(model+0xa8,0x100)
        for i,(activity,weight) in enumerate(sequences):
            self.write(model+0x100+i*176+40,activity);self.write(model+0x100+i*176+44,weight)
        self.events=[];self.seed=seed
        sp=shared.STACK+0xff00
        for i,value in enumerate((shared.EDGE+0xff00,model,self.pev(1),query)):self.write(sp+4*i,value)
        self.uc.reg_write(shared.UC_X86_REG_ESP,sp)
        self.uc.emu_start(0x1000f450,shared.EDGE+0xff00,count=20000)
        assert self.uc.reg_read(shared.UC_X86_REG_EIP)==shared.EDGE+0xff00
        return [self.uc.reg_read(shared.UC_X86_REG_EAX),len(self.events),*self.events]


def generate(sample,proof,cases,expected):
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    pre=json.loads(proof.read_text('utf8'))
    if not pre['beforeImplementation'] or digest(sample)!=pre['sampleSha256'] or digest(sample)!=shared.SHA:
        raise ValueError('Original identity/pre-edit proof differs')
    spec=importlib.util.spec_from_file_location('activity_mapper',ROOT/'tests/cryoffear-magazine-actions/run.py')
    mapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(mapper)
    image=mapper.mapped_image(sample.read_bytes())
    for r in pre['regions']:
        b=bytes.fromhex(r['hex']);at=int(r['va'],16)-shared.BASE
        if image[at:at+len(b)]!=b or hashlib.sha256(b).hexdigest()!=r['sha256']:raise ValueError('IDA bytes differ')
    for name,value in pre['archives'].items():
        if digest(proof.parent/name)!=value:raise ValueError('IDA archive differs')
    machine=Activity(sample);rng=random.Random(0xf450)
    for i in range(768):
        count=i%65;query=rng.choice((1,3,26,70,72,73,74,75,76,777,-1))&0xffffffff;seed=rng.getrandbits(32)
        weights=(0,1,2,3,7) if i%3 else (0,-1,1,-2,2147483647,-2147483648)
        sequences=[(rng.choice((query,query,query,1,3,26,777))&0xffffffff,rng.choice(weights)&0xffffffff) for _ in range(count)]
        row=[count,query,seed,*[v for s in sequences for v in s]]
        cases.write(' '.join(f'{v:08x}' for v in row)+'\n')
        expected.write(' '.join(f'{v:08x}' for v in machine.run(query,seed,sequences))+'\n')
    model_cases=0
    for model in pre.get('models',[]):
        path=ROOT/model['path']
        if digest(path)!=model['sha256']:raise ValueError('Actual model identity differs')
        data=path.read_bytes();count,offset=struct.unpack_from('<ii',data,164)
        if count<=0 or offset<244 or count>(len(data)-offset)//176:raise ValueError('Bad actual model')
        sequences=[struct.unpack_from('<II',data,offset+i*176+40) for i in range(count)]
        for query in sorted({a for a,w in sequences}|{26,70,72,73,74,75,76,777}):
            for seed in (0,11,0xffffffff):
                row=[count,query,seed,*[v for s in sequences for v in s]]
                cases.write(' '.join(f'{v:08x}' for v in row)+'\n')
                expected.write(' '.join(f'{v:08x}' for v in machine.run(query,seed,sequences))+'\n');model_cases+=1
    missing=[hex(at) for at in sorted(machine.visited) if not 0x1000f450<=at<0x1000f4c4]
    if missing:raise ValueError('Unpinned executed instructions: '+','.join(missing))
    return dict(sampleSha256=shared.SHA,proofSha256=digest(proof),cases=768+model_cases,actualModelCases=model_cases,
        function='1000F450..1000F4C4',visitedInstructions=len(machine.visited),uncoveredInstructions=missing,
        boundary='Complete original non-null model consumer; native RNG is controlled and every call compared')
