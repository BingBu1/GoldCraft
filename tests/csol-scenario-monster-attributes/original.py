"""Bounded original scalar instructions versus production attributes only.

No original engine or factory is called. Type, round, slot, entity and manager
memory are explicit controlled inputs; mode-specific paths are not executed.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import random
import struct
import subprocess
import sys
sys.dont_write_bytecode = True
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP,
    UC_X86_REG_EFLAGS, UC_X86_REG_MXCSR, UC_X86_REG_XMM0)

helper = Path(__file__).parents[1]/'csol-scenario/spawn_points_original.py'
spec = importlib.util.spec_from_file_location('frozen_scalar_binary_support', helper)
support = importlib.util.module_from_spec(spec); spec.loader.exec_module(support)
MANAGER = 0x20000000
ENTITY = 0x20002000
PEV = 0x20004000
TYPE = 0x20005000
ROUNDS = 0x20006000
SLOT = 0x20007000
STACK = 0x21008000
RANGES = [(0x113834d0,0x11383526,80), (0x113842fb,0x1138439e,120), (0x1138444b,0x11384459,20)]

class OriginalAttributes:
    original = support.Original.original

    def __init__(self, path, metadata):
        self.data = path.read_bytes(); self.meta = json.loads(metadata.read_text(encoding='utf-8-sig'))
        assert hashlib.sha256(self.data).hexdigest() == support.EXPECTED_MP == self.meta['sha256']
        assert int(self.meta['image_base'],16) == 0x10000000
        self.u = Uc(UC_ARCH_X86,UC_MODE_32)
        for start, size in [(0x11383000,0x2000),(0x117c2000,0x1000)]:
            self.u.mem_map(start,size); self.u.mem_write(start,self.original(start,size))
        self.u.mem_map(MANAGER,0x10000); self.u.mem_map(0x21000000,0x10000)
        self.stop_at = self.stopped_at = None
        for _,end,_ in RANGES: self.u.hook_add(UC_HOOK_CODE,self.stop,begin=end,end=end)
        assert self.u.mem_read(0x117c25f8,4) == struct.pack('<f',0.4)

    def stop(self,u,address,size,unused):
        if address == self.stop_at: self.stopped_at = address; u.emu_stop()

    def put(self,address,value,size=4):
        self.u.mem_write(address,(value & ((1<<(size*8))-1)).to_bytes(size,'little'))

    def get(self,address):
        return int.from_bytes(self.u.mem_read(address,4),'little')

    def run(self,start,end,limit):
        self.stop_at=end; self.stopped_at=None
        self.u.emu_start(start,0,count=limit)
        assert self.stopped_at == end and self.u.reg_read(UC_X86_REG_EIP) == end, 'bounded code did not reach explicit boundary'
        assert self.u.reg_read(UC_X86_REG_ESP) == STACK, 'scalar slice unexpectedly changed stack'

    def case(self,values):
        health,armor,damage,attack_range,interval,speed,money,*delta=values
        self.u.mem_write(MANAGER,bytes(0x10000))
        self.u.mem_write(0x21000000,bytes(0x10000))
        for register in (UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,
            UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP,UC_X86_REG_ESP): self.u.reg_write(register,0)
        self.u.reg_write(UC_X86_REG_EFLAGS,2); self.u.reg_write(UC_X86_REG_MXCSR,0x1f80)
        self.u.reg_write(UC_X86_REG_XMM0,0)
        self.u.reg_write(UC_X86_REG_ESP,STACK); self.u.reg_write(UC_X86_REG_EBP,STACK+0x100)
        row_index=3; row=ROUNDS+row_index*44
        for offset,value,size in [(0x10,health,4),(0x14,armor,4),(6,damage,2),(8,attack_range,2),
            (0xc,interval,4),(0x18,speed,2),(0x1a,money,2)]: self.put(TYPE+offset,value,size)
        for offset,value in zip([0xe,0x10,0x12,0x14,0x16,0x18,0x1a],delta): self.put(row+offset,value,2)
        self.put(ENTITY+8,PEV); self.put(MANAGER+0xd4,ROUNDS)
        self.put(STACK+0x108,TYPE)
        self.u.reg_write(UC_X86_REG_EDX,TYPE); self.u.reg_write(UC_X86_REG_ESI,row)
        self.u.reg_write(UC_X86_REG_EBX,ENTITY); self.u.reg_write(UC_X86_REG_EDI,MANAGER)
        self.run(*RANGES[0])
        assert self.get(PEV+0x178) == self.get(PEV+0x1dc), 'original maxhealth copy'
        self.u.reg_write(UC_X86_REG_EBX,SLOT); self.u.reg_write(UC_X86_REG_EDX,row_index*44)
        self.u.reg_write(UC_X86_REG_ESI,ENTITY)
        self.run(*RANGES[1]); self.run(*RANGES[2])
        assert self.get(PEV+0x240) == self.get(SLOT+0x1c), 'original speed copy'
        return [self.get(PEV+0x178),self.get(PEV+0x1e8),self.get(SLOT+0x10),self.get(SLOT+8),
            self.get(SLOT+0x14),self.get(SLOT+0x1c),self.get(SLOT+0xc)]

def cases():
    base=[50,40,10,60,0x40400000,240,300,0,0,0,0,0,0,0]
    result=[base.copy()]
    int32s=[-2147483648,-2147483647,-32768,-1,0,9,10,11,16777215,16777216,16777217,2147483646,2147483647]
    shorts=[-32768,-32767,-391,-1,0,1,389,390,391,32766,32767]
    for field,delta,values in [(0,9,int32s),(1,10,int32s),(2,7,shorts),(3,8,shorts),(5,11,shorts),(6,13,shorts)]:
        for value in values:
            for change in [-32768,-1,0,1,32767]:
                row=base.copy(); row[field]=value; row[delta]=change; result.append(row)
    for bits in [0xff7fffff,0xbf800000,0x80800000,0x80000001,0x80000000,0,1,0x00800000,
        0x3ecccccc,0x3ecccccd,0x3eccccce,0x3f000000,0x40400000,0x7f7fffff]:
        for change in [-32768,0,32767]:
            row=base.copy(); row[4]=bits; row[12]=change; result.append(row)
    rng=random.Random(0xc5012026)
    for _ in range(64):
        row=[rng.randint(-2147483648,2147483647),rng.randint(-2147483648,2147483647),
            rng.randint(-32768,32767),rng.randint(-32768,32767),rng.choice([0,0x3ecccccd,0x40400000]),
            rng.randint(-32768,32767),rng.randint(-32768,32767)]
        row.extend(rng.randint(-32768,32767) for _ in range(7)); result.append(row)
    return result

def main():
    parser=argparse.ArgumentParser()
    for name in ['mp','metadata','exe','output']: parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args(); original=OriginalAttributes(args.mp,args.metadata)
    matrix=cases(); expected=[original.case(row) for row in matrix]
    result=subprocess.run([str(args.exe.resolve()),'--oracle'],input='\n'.join(' '.join(map(str,row)) for row in matrix)+'\n',
        text=True,capture_output=True,timeout=30)
    actual=[list(map(int,line.split())) for line in result.stdout.splitlines()]
    failures=[{'case':row,'originalBits':want,'production':got} for row,want,got in zip(matrix,expected,actual) if [1,*want]!=got]
    complete=result.returncode==0 and len(actual)==len(expected)
    evidence={'scope':'base mode15/SubType1 arithmetic slices only, finite interval inputs, MXCSR 0x1f80',
        'cases':len(matrix),'complete':complete,'failures':failures,'returnCode':result.returncode,'stderr':result.stderr,
        'ranges':[{'start':hex(start),'endExclusive':hex(end),'instructionLimit':limit,
            'sha256':hashlib.sha256(original.original(start,end-start)).hexdigest()} for start,end,limit in RANGES],
        'stopping':'explicit code hook, EIP and unchanged stack for every slice',
        'constant':{'address':'0x117c25f8','bytesHex':original.original(0x117c25f8,4).hex()},
        'inputs':[support.evidence(path) for path in [args.mp,args.metadata,args.exe,Path(__file__),helper]],
        'substitutes':['caller supplies manager/type/round/entity/slot pointers and physical round offset',
            'mode gates deliberately outside bounded slices; no other-mode multipliers',
            'all engine services and actual entity construction outside slices'],
        'nativeDllLoaded':False,'roundParserExecutedByOriginal':False,'liveGameplay':False}
    args.output.write_text(json.dumps(evidence,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'cases':len(matrix),'complete':complete,'failures':len(failures)}))
    return 0 if complete and not failures else 1
if __name__=='__main__':raise SystemExit(main())
