#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay the original CoF filter/scale/zoom stage from the SHA-pinned PE."""
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
from unicorn.x86_const import UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_ESP,UC_X86_REG_XMM1,UC_X86_REG_FPCW,UC_X86_REG_MXCSR

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('cof_fov_sample',ROOT/'tests/cryoffear-aim/oracle.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
CASE=struct.Struct('<4i3f2i')
OUTPUT=struct.Struct('<4i')


def f32(value):
    return struct.unpack('<f',struct.pack('<f',value))[0]


def cases():
    integers=[-2147483648,-16777217,-101,-3,-1,0,1,3,101,16777217,2147483647]
    for current,previous,filter_value,sensitivity,zoom in itertools.product(
            integers,[-2147483648,-3,0,3,2147483647],[0,1],[-2.0,0.0,0.1,0.75,1.0,2.5],[False,True]):
        yield (current,previous,previous,current,filter_value,0,sensitivity,50 if zoom else 90,90)
    floats=[0.0,-0.0,1.0,-1.0,1e-35,-1e-35,1e35,-1e35,float('nan'),float('inf'),-float('inf')]
    for filter_value,hud_sensitivity,sensitivity in itertools.product(floats,floats,[-1.0,0.0,2.5,float('nan')]):
        yield (16777217,-16777217,3,-3,filter_value,hud_sensitivity,sensitivity,50,90)
    rng=random.Random(120711)
    for _ in range(4000):
        yield (*(rng.randrange(-2147483648,2147483648) for _ in range(4)),
               rng.choice([0,0,1,2,-1]),f32(rng.choice([0,0,rng.uniform(-4,4)])),
               f32(rng.uniform(-4,4)),rng.randrange(-10,200),rng.randrange(1,180))


def verify(sample,runner,output):
    original=base.Original(sample)
    uc=original.uc
    uc.reg_write(UC_X86_REG_FPCW,0x27f);uc.reg_write(UC_X86_REG_MXCSR,0x1f80)
    initial=uc.context_save()
    raw=b''.join(CASE.pack(*row) for row in cases())
    expected=bytearray()
    scalar=base.CVARS+0x100
    uc.mem_write(0x10543558,base.u32(scalar))
    uc.mem_write(0x1054355c,base.u32(scalar+0x20))
    for at in range(0,len(raw),CASE.size):
        row=CASE.unpack_from(raw,at)
        x,y,previous_x,previous_y,filter_value,hud_sensitivity,sensitivity,fov,default=row
        uc.context_restore(initial)
        uc.reg_write(UC_X86_REG_EBP,base.STACK+0x2000)
        uc.reg_write(UC_X86_REG_ESP,base.STACK+0x1000)
        uc.reg_write(UC_X86_REG_ESI,x&0xffffffff);uc.reg_write(UC_X86_REG_EDI,y&0xffffffff)
        uc.reg_write(UC_X86_REG_XMM1,0)
        uc.mem_write(0x10543578,struct.pack('<2i',previous_x,previous_y))
        uc.mem_write(scalar+12,struct.pack('<f',filter_value))
        uc.mem_write(scalar+0x2c,struct.pack('<f',sensitivity))
        uc.mem_write(0x1018a0d4,struct.pack('<f',hud_sensitivity))
        uc.mem_write(0x1018d1e0,struct.pack('<i',fov));uc.mem_write(0x1018fec0,struct.pack('<i',default))
        uc.emu_start(0x1007b635,0x1007b792,count=400)
        expected.extend(uc.mem_read(0x10543570,16))
    actual=subprocess.run([str(runner)],input=raw,capture_output=True,timeout=30)
    actual.check_returncode()
    if len(actual.stdout)!=len(expected):raise AssertionError('Mouse output length differs')
    mismatches=[i//OUTPUT.size for i in range(0,len(expected),OUTPUT.size)
                if expected[i:i+OUTPUT.size]!=actual.stdout[i:i+OUTPUT.size]]
    result=dict(success=not mismatches,cases=len(raw)//CASE.size,mismatches=mismatches[:24],
        originalSha256=base.SHA,stage='1007B635..1007B792; actual1006CDC0 getter executes',
        originalBytesSha256=hashlib.sha256(original.uc.mem_read(0x1007b635,0x1007b792-0x1007b635)).hexdigest(),
        outputSha256=hashlib.sha256(actual.stdout).hexdigest(),
        boundary='Original arithmetic stage and raw-history updates only; device input, native hook, gameplay and performance are separate')
    output.write_text(json.dumps(result,indent=2),'utf8')
    if mismatches:raise AssertionError(str(result))
    return result
