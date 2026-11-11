#!/usr/bin/env python3
"""Probe untouched original Aurora initialization with different raw heap seeds.

Requires the private SHA-pinned module and script. No game bytes are published.
Only allocator, engine tokenizer/resource calls and CRT leaf services are modeled.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("aim_oracle", ROOT/"tests/cryoffear-aim/oracle.py")
base=importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
HEAP=0x22000000
SCRIPT=base.CVARS
SYS=base.HUD
CURSOR=base.HUD+0x3000
CONTROL={"bounceFlag":(0x24,1),"yawMode":(0x12C,4),"inheritAngle":(0x130,4),"waterOnly":(0x134,4)}
TOKEN=re.compile(rb'\s*(?://[^\r\n]*(?:[\r\n]|$)\s*)*(?:"([^"\x00]*)"|([{}])|([^\s{}"\x00]+))')

class Probe(base.Original):
    def __init__(self,sample,pattern):
        super().__init__(sample)
        self.pattern=pattern
        self.next=HEAP
        self.allocations=[]
        self.uc.mem_map(HEAP,0x10000)
        self.uc.mem_write(base.STUB+0x200,b"\xC3")
        self.uc.mem_write(base.STUB+0x210,b"\xDD\x05"+base.u32(base.DATA+0x100)+b"\xC3")
        self.uc.mem_write(0x101B2748,base.u32(base.STUB+0x200))
        self.uc.mem_write(0x101B2608,base.u32(base.STUB+0x240))
        self.uc.mem_write(base.STUB+0x240,b"\xC3")
        self.uc.mem_write(base.STUB+0x280,b"\xD9\x44\x24\x04\xC3")
        self.uc.mem_write(0x101B2714,base.u32(base.STUB+0x280))
        for slot,stub,entity in ((0x101B26D8,base.STUB+0x2A0,HEAP+0x8000),
                                 (0x101B26DC,base.STUB+0x2C0,HEAP+0x9000)):
            self.uc.mem_write(slot,base.u32(stub))
            self.uc.mem_write(stub,b"\xB8"+base.u32(entity)+b"\xC3")
        self.uc.hook_add(UC_HOOK_CODE,self.service)
    def string(self,address):
        if not address:return b""
        result=bytearray()
        for i in range(4096):
            byte=self.uc.mem_read(address+i,1)[0]
            if not byte:return bytes(result)
            result.append(byte)
        raise AssertionError("unterminated original string")
    def args(self,n):
        return struct.unpack("<"+"I"*n,self.uc.mem_read(self.uc.reg_read(UC_X86_REG_ESP)+4,4*n))
    def ret(self,value=0):
        uc=self.uc
        sp=uc.reg_read(UC_X86_REG_ESP)
        address=struct.unpack("<I",uc.mem_read(sp,4))[0]
        uc.reg_write(UC_X86_REG_EAX,value)
        uc.reg_write(UC_X86_REG_ESP,sp+4)
        uc.reg_write(UC_X86_REG_EIP,address)
    def allocate(self,size):
        addr=self.next
        self.next=(addr+size+15)&~15
        assert self.next<HEAP+0x10000
        self.uc.mem_write(addr,bytes([self.pattern])*size)
        self.allocations.append((addr,size))
        return addr
    def service(self,uc,address,size,_):
        if address==0x100B44CB:
            self.ret(self.allocate(self.args(1)[0]))
        elif address==0x100B42A4:
            self.ret()
        elif address==0x1010B99C:
            a,b=self.args(2)
            left,right=self.string(a),self.string(b)
            self.ret(0 if left==right else (1 if left>right else 0xFFFFFFFF))
        elif address==0x100FE800:
            dest,source,count=self.args(3)
            text=self.string(source)[:count]
            uc.mem_write(dest,text+bytes(max(0,count-len(text))))
            self.ret(dest)
        elif address==0x100F7B50:
            text=self.string(self.args(1)[0])
            value=float(text or b"0")
            uc.mem_write(base.DATA+0x100,struct.pack("<d",value))
            uc.reg_write(UC_X86_REG_EIP,base.STUB+0x210)
        elif address==0x100F9A9C:
            self.ret(int(self.string(self.args(1)[0])))
        elif address==base.STUB+0x240:
            self.ret(1)
        elif address==base.STUB+0x200:
            data,out=self.args(2)
            if not data:self.ret(0);return
            text=self.string(data)
            match=TOKEN.match(text)
            if not match:
                uc.mem_write(out,b"\0"); self.ret(0); return
            token=next(x for x in match.groups() if x is not None)
            uc.mem_write(out,token+b"\0")
            self.ret(data+match.end())
        elif address==0x10082461:
            # Security-cookie verification is outside the parser's memory writes.
            uc.reg_write(UC_X86_REG_EIP,0x10082466)
    def call(self,address,args=(),this=0):
        sp=base.STACK+0x2000
        self.uc.mem_write(sp,base.u32(base.STOP)+b"".join(base.u32(x) for x in args))
        self.uc.reg_write(UC_X86_REG_ESP,sp)
        self.uc.reg_write(UC_X86_REG_ECX,this)
        self.uc.emu_start(address,base.STOP,count=100000)
        return self.uc.reg_read(UC_X86_REG_EAX)
    def controls(self,p):
        return {k:self.uc.mem_read(p+off,size).hex() for k,(off,size) in CONTROL.items()}
    def run(self,script):
        uc=self.uc
        uc.mem_write(SYS,bytes(0x4000))
        self.script=script
        uc.mem_write(SCRIPT,script+b"\0")
        # Original maintype lookup creates a placeholder, then parser overwrites it.
        uc.mem_write(SCRIPT+0xF00,b"sparker\0")
        placeholder=self.call(0x10081760,(SCRIPT+0xF00,),SYS)
        after_ctor=self.controls(placeholder)
        first=script.index(b"{")+1
        uc.mem_write(CURSOR,base.u32(SCRIPT+first))
        self.call(0x100822E0,(CURSOR,),SYS)
        after_parser=self.controls(placeholder)
        cursor=struct.unpack("<I",uc.mem_read(CURSOR,4))[0]
        remainder=self.string(cursor)
        second=remainder.index(b"{")+1
        uc.mem_write(CURSOR,base.u32(cursor+second))
        self.call(0x100822E0,(CURSOR,),SYS)
        # Main pool allocation only links slots; use original operator new[] chain.
        self.call(0x100817C0,(20,),SYS)
        pool=struct.unpack("<I",uc.mem_read(SYS+0xB4,4))[0]
        positions=[uc.mem_read(pool+0x88*i+0xC,12).hex() for i in range(20)]
        # Find actual parser-created smoke type through original spraytype pointer.
        sparks=struct.unpack("<I",uc.mem_read(placeholder+0x128,4))[0]
        result=dict(pattern=self.pattern,constructorControls=after_ctor,
                    mainControlsAfterParser=after_parser,sparksControlsAfterParser=self.controls(sparks),
                    mainSprayRate=list(struct.unpack("<2f",uc.mem_read(placeholder+0xF8,8))),
                    mainSprayPitch=list(struct.unpack("<2f",uc.mem_read(placeholder+0x110,8))),
                    sparksSprite=struct.unpack("<I",uc.mem_read(sparks+0x15C,4))[0],
                    poolPositions=positions,allocatedTypes=[size for _,size in self.allocations if size==404])
        for controls in (after_ctor,after_parser,self.controls(sparks)):
            for name,(_,length) in CONTROL.items():assert controls[name]==bytes([self.pattern]*length).hex()
        assert all(p==bytes([self.pattern]*12).hex() for p in positions)
        uc.mem_write(SYS+0xC,base.u32(1))
        uc.mem_write(SYS+0x10,base.u32(1))
        uc.mem_write(SYS+0x14,base.u32(0))
        uc.mem_write(SYS+0xC4,base.u32(placeholder))
        self.call(0x10083C00,(base.bits(0.016),0),SYS)
        main=struct.unpack("<I",uc.mem_read(SYS+0xBC,4))[0]
        result["firstFramePositionBits"]=list(struct.unpack("<3I",uc.mem_read(main+0xC,12)))
        result["firstFrameVelocityBits"]=list(struct.unpack("<3I",uc.mem_read(main+0x18,12)))
        return result

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--sample",type=Path,required=True)
    p.add_argument("--script",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    args=p.parse_args()
    script=args.script.read_bytes()
    assert hashlib.sha256(script).hexdigest()=="e37c7e21896e7d206ca020d60b263bac1dac193c600bbb18d7ca4d205f299fb7"
    rows=[Probe(args.sample,pattern).run(script) for pattern in (0,0xA5,0xFF)]
    args.output.write_text(json.dumps(dict(sampleSha256=base.SHA,
        scriptSha256=hashlib.sha256(script).hexdigest(),rawHeapSeedProbes=rows,
        conclusion="Original constructor and actual script parser preserve raw heap controls; pool preserves raw positions. No stable zero default established.",
        modeledServices=["allocator raw pattern", "CRT strcmp/strncpy/atof/atoi", "engine COM_ParseFile for actual script", "engine SPR_Load success"],
        securityCookieCheckSkipped=True,gameplay=False),indent=2),encoding="utf8")
    print("Aurora: three raw heap patterns survive original constructor, actual script parser and all 20 pool positions")
if __name__=="__main__":main()
