#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare three actual CoF crosshair rectangles with the original draw stage."""
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import struct
import subprocess
import unicorn
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EAX,UC_X86_REG_EIP

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('crosshair_sample',ROOT/'tests/cryoffear-aim/oracle.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
CASE=struct.Struct('<5ifI')
OUTPUT=struct.Struct('<13i')


def cases():
    for width,height,scalar in itertools.product([99,100,320,640,801,1024,1280,1920,2560,3840,7680],
            [240,480,601,720,1080,1200,2160,4320],range(11)):
        yield (width,height,3,0,0,1.0,scalar)
    for weapon,aiming,telescope,enabled,scalar in itertools.product([3,7,19,21],
            [0,1],[0,1],[0.0,-0.0,1.0,-1.0,float('nan'),float('inf')],[0,1,7,10]):
        yield (1920,1200,weapon,aiming,telescope,enabled,scalar)


class Original:
    def __init__(self,sample):
        self.original=base.Original(sample);self.uc=uc=self.original.uc
        self.bars=[]
        uc.mem_write(0x101b2634,base.u32(base.STUB+0x1000))
        uc.mem_write(0x105433f4,base.u32(base.CVARS+0x100))
        uc.mem_write(0x101b2600,base.u32(base.HUD))
        uc.mem_write(base.HUD+0x146c,base.u32(base.HUD+0x2000))
        uc.mem_write(base.STUB+0x1000,b'\xc3')
        uc.hook_add(unicorn.UC_HOOK_CODE,self.hook)
        self.cpu=uc.context_save()

    def hook(self,uc,address,size,context):
        stack=uc.reg_read(UC_X86_REG_ESP)
        if address==0x100735b0:
            target=struct.unpack('<I',uc.mem_read(stack,4))[0]
            uc.reg_write(UC_X86_REG_ESP,stack+4);uc.reg_write(UC_X86_REG_EIP,target)
        elif address==base.STUB+0x1000:
            values=struct.unpack('<8i',uc.mem_read(stack+4,32))
            if values[4:]!=(255,0,0,200):raise AssertionError('Original crosshair color/alpha changed')
            self.bars.append(values[:4])

    def run(self,row):
        width,height,weapon,aiming,telescope,enabled,scalar=row;uc=self.uc
        uc.context_restore(self.cpu);self.bars=[]
        uc.mem_write(0x1018f430,struct.pack('<2i',width,height))
        uc.mem_write(0x1018c9f4,bytes([aiming]))
        uc.mem_write(base.HUD+0x2000+188,struct.pack('<i',telescope))
        uc.mem_write(base.CVARS+0x10c,struct.pack('<f',enabled))
        uc.mem_write(0x101b2f20,struct.pack('<f',float(scalar)))
        uc.mem_write(base.HUD+0x3000+156,struct.pack('<i',weapon))
        uc.reg_write(UC_X86_REG_ESP,base.STACK+0x1000)
        uc.reg_write(UC_X86_REG_EBP,base.STACK+0x2000)
        uc.reg_write(UC_X86_REG_ESI,base.HUD+0x3000)
        uc.reg_write(UC_X86_REG_EAX,weapon)
        uc.emu_start(0x1001952d,0x100196c2,count=400)
        return OUTPUT.pack(len(self.bars),*(x for bar in self.bars for x in bar),*([0]*(12-len(self.bars)*4)))


def verify(sample,runner,output):
    oracle=Original(sample);rows=list(cases());binary=b''.join(CASE.pack(*row) for row in rows)
    expected=b''.join(oracle.run(CASE.unpack(CASE.pack(*row))) for row in rows)
    actual=subprocess.run([str(runner)],input=binary,capture_output=True,timeout=30)
    actual.check_returncode()
    if len(actual.stdout)!=len(expected):raise AssertionError('Rectangle output count differs')
    mismatches=[i//OUTPUT.size for i in range(0,len(expected),OUTPUT.size)
        if actual.stdout[i:i+OUTPUT.size]!=expected[i:i+OUTPUT.size]]
    result=dict(success=not mismatches,cases=len(rows),mismatches=mismatches[:12],originalSha256=base.SHA,
        stage='1001952D..100196C2; original shader-free FillRGBA calls and inner aim/telescope/cvar gates',
        originalBytesSha256=hashlib.sha256(oracle.uc.mem_read(0x1001952d,405)).hexdigest(),
        outputSha256=hashlib.sha256(actual.stdout).hexdigest(),
        boundary='Original magazine-pistol stage. Outer hide/HUD/weapon gates and actual engine drawing validated separately.')
    output.write_text(json.dumps(result,indent=2),'utf8')
    if mismatches:raise AssertionError(str(result))
    return result
