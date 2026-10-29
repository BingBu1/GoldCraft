"""Execute only the original 33-slot cache routine with a bounded lookup hook."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import random
import struct
import subprocess
import sys
sys.dont_write_bytecode=True
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI
helper=Path(__file__).parents[1]/'csol-scenario/spawn_points_original.py'
spec=importlib.util.spec_from_file_location('sequence_binary_reader',helper)
support=importlib.util.module_from_spec(spec);spec.loader.exec_module(support)
SERVICE=0x20000000;ENTITY=0x20001000;TYPE=0x20002000;STACK=0x21008000;STOP=0x22000000
ENTRY=0x113ee880;END=0x113eeb82;LOOKUP=0x113f8670

class Original:
    original=support.Original.original
    def __init__(self,mp,metadata,labels):
        self.data=mp.read_bytes();self.meta=json.loads(metadata.read_text(encoding='utf-8-sig'))
        assert hashlib.sha256(self.data).hexdigest()==support.EXPECTED_MP==self.meta['sha256']
        self.contract=json.loads(labels.read_text(encoding='utf-8'))['cache'];assert len(self.contract)==33
        self.labels=[row['name'] for row in self.contract]
        self.offsets=[int(row['typeOffset'],16) for row in self.contract];assert len(set(self.offsets))==33
        self.u=Uc(UC_ARCH_X86,UC_MODE_32)
        pages={ENTRY & ~4095,LOOKUP & ~4095}
        pages.update(int(row['stringAddress'],16)&~4095 for row in self.contract);pages.add(0x117a3000)
        for address in sorted(pages):self.u.mem_map(address,4096);self.u.mem_write(address,self.original(address,4096))
        for address in [SERVICE,0x21000000,STOP]:self.u.mem_map(address,0x10000 if address!=STOP else 4096)
        self.u.hook_add(UC_HOOK_CODE,self.lookup,begin=LOOKUP,end=LOOKUP)
        self.u.hook_add(UC_HOOK_CODE,self.stop,begin=STOP,end=STOP)
    def get(self,address):return int.from_bytes(self.u.mem_read(address,4),'little')
    def put(self,address,value):self.u.mem_write(address,(value&0xffffffff).to_bytes(4,'little'))
    def stop(self,u,address,size,unused):self.stopped=True;u.emu_stop()
    def lookup(self,u,address,size,unused):
        assert u.reg_read(UC_X86_REG_ECX)==ENTITY
        sp=u.reg_read(UC_X86_REG_ESP);pointer=self.get(sp+4);label=bytearray()
        for offset in range(64):
            value=self.u.mem_read(pointer+offset,1)
            if value==b'\0':break
            label.extend(value)
        else:raise AssertionError('original label lacks bounded terminator')
        name=label.decode('ascii')
        index=len(self.calls);assert index<34
        assert name==(self.labels[index] if index<33 else 'death')
        self.calls.append(name);u.reg_write(UC_X86_REG_EAX,self.results[index]&0xffffffff)
        u.reg_write(UC_X86_REG_ESP,sp+8);u.reg_write(UC_X86_REG_EIP,self.get(sp))
    def case(self,results):
        self.results=results;self.calls=[];self.stopped=False
        self.u.mem_write(TYPE,bytes([0xa5])*512);self.put(SERVICE+0x1c,ENTITY)
        self.put(STACK,STOP);self.put(STACK+4,TYPE)
        self.u.reg_write(UC_X86_REG_ECX,SERVICE);self.u.reg_write(UC_X86_REG_ESP,STACK)
        for reg in [UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI]:self.u.reg_write(reg,0x12345678)
        self.u.emu_start(ENTRY,0,count=1000)
        assert self.stopped and self.u.reg_read(UC_X86_REG_EIP)==STOP and self.u.reg_read(UC_X86_REG_ESP)==STACK+8
        for reg in [UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI]:assert self.u.reg_read(reg)==0x12345678
        memory=bytes(self.u.mem_read(TYPE,512));allowed={offset+i for offset in self.offsets for i in (0,1)}
        assert all(value==0xa5 for i,value in enumerate(memory) if i not in allowed),'original touched a gap or unrelated type field'
        return [int.from_bytes(memory[offset:offset+2],'little',signed=True) for offset in self.offsets],self.calls.copy()

def matrix():
    base=[1000+i for i in range(34)];rows=[base.copy()]
    for slot in range(33):
        for value in [-1,65535,131071,-65537,32767,32768,65536,2147483647,-2147483648]:
            row=base.copy();row[slot]=value;rows.append(row)
    for death in [-1,0,32767,32768,65535,65536,131071,-2147483648,2147483647]:
        row=base.copy();row[15]=-1;row[33]=death;rows.append(row)
    for mask in range(16):
        row=base.copy()
        for bit,slot in enumerate([15,30,31,32]):
            if mask&(1<<bit):row[slot]=65535
        row[0]=-32768;row[1]=65536;rows.append(row)
    rows.extend([[value]*34 for value in [-1,0,65535]])
    rng=random.Random(0xc5010033)
    for _ in range(32):rows.append([rng.choice([-1,65535,32768,rng.randint(-2147483648,2147483647)]) for _ in range(34)])
    return rows
def main():
    parser=argparse.ArgumentParser()
    for name in ['mp','metadata','labels','exe','output']:parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();original=Original(args.mp,args.metadata,args.labels);cases=matrix()
    expected=[original.case(row) for row in cases]
    run=subprocess.run([str(args.exe.resolve()),'--oracle'],input='\n'.join(' '.join(map(str,row)) for row in cases)+'\n',text=True,capture_output=True,timeout=30)
    actual=[]
    for line in run.stdout.splitlines():
        parts=line.split();actual.append((list(map(int,parts[:35])),parts[35:]))
    failures=[]
    for i,((values,calls),(got,names)) in enumerate(zip(expected,actual)):
        if got!=[1,len(calls),*values] or names!=calls:failures.append({'case':cases[i],'expected':values,'expectedNames':calls,'actual':got,'actualNames':names})
    complete=run.returncode==0 and len(actual)==len(expected)
    record={'scope':'original complete type sequence cache only; LookupSequence wrapper replaced with per-call oracle values',
        'cases':len(cases),'failures':failures,'complete':complete,'returnCode':run.returncode,'stderr':run.stderr,
        'original':{'entry':hex(ENTRY),'endExclusive':hex(END),'sha256':hashlib.sha256(original.original(ENTRY,END-ENTRY)).hexdigest(),
            'instructionLimit':1000,'checks':['STOP hook','EIP','ret4 stack','callee-saved EBP/ESI/EDI','non-cache type byte canaries']},
        'inputs':[support.evidence(path) for path in [args.mp,args.metadata,args.labels,args.exe,Path(__file__),helper]],
        'substitutes':['LookupSequence at113F8670; no model, string cache or engine lookup executed'],
        'liveGameplay':False,'nativeDllLoaded':False}
    args.output.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'cases':len(cases),'complete':complete,'failures':len(failures)}))
    return 0 if complete and not failures else 1
if __name__=='__main__':raise SystemExit(main())
