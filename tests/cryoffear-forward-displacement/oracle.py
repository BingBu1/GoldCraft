# SPDX-License-Identifier: GPL-3.0-or-later
"""Run original clamp, viewmodel consumer and complete WpnDeploy reset."""
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import struct
import subprocess
import unicorn
from unicorn.x86_const import UC_X86_REG_EBP,UC_X86_REG_ESP,UC_X86_REG_ESI,UC_X86_REG_EIP,UC_X86_REG_XMM0,UC_X86_REG_XMM1

ROOT=Path(__file__).resolve().parents[2]


def verify(sample,runner,output):
    spec=importlib.util.spec_from_file_location('original',ROOT/'tests/cryoffear-aim/oracle.py')
    base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
    original=base.Original(sample);u=original.uc;heap=0x22000000;u.mem_map(heap,0x2000)
    kills=[]
    def hook(uc,address,size,context):
        if address==0x10080B90:
            sp=uc.reg_read(UC_X86_REG_ESP);ret,arg=struct.unpack('<2I',uc.mem_read(sp,8));assert arg==1
            kills.append(bytes(uc.mem_read(0x1055959C,4)))
            # Actual KillLocal is thiscall and returns with ret4.
            uc.reg_write(UC_X86_REG_ESP,sp+8);uc.reg_write(UC_X86_REG_EIP,ret)
    u.hook_add(unicorn.UC_HOOK_CODE,hook)
    def reset():
        sp=base.STACK+0x2000;u.mem_write(sp,struct.pack('<4I',base.STOP,0,0,0));u.reg_write(UC_X86_REG_ESP,sp)
        u.emu_start(0x1001B4B0,base.STOP,count=100);assert u.reg_read(UC_X86_REG_EIP)==base.STOP
    rng=random.Random(0xC0F10559);rows=[]
    for i in range(2048):
        vals=[rng.uniform(-10,10),*[rng.uniform(-1000,1000) for _ in range(3)],*[rng.uniform(-1,1) for _ in range(3)]]
        rows.append([i%3==0,*struct.unpack('<7I',struct.pack('<7f',*vals))])
    for field in range(7):
        for special in (0,0x80000000,1,0x80000001,0x7F800000,0xFF800000,0x7FC12345,0x7F812345):
            row=[0,base.bits(2),base.bits(1),base.bits(2),base.bits(3),base.bits(.25),base.bits(-.5),base.bits(.75)]
            row[field+1]=special;rows.append(row)
    expected=[]
    for row in rows:
        u.mem_write(0x1055959C,base.u32(row[1]))
        if row[0]:reset()
        u.reg_write(UC_X86_REG_ESP,base.STACK+0x1800);u.emu_start(0x100AE110,0x100AE145,count=30)
        state=bytes(u.mem_read(0x1055959C,4));bp=base.STACK+0x1000
        u.reg_write(UC_X86_REG_EBP,bp);u.reg_write(UC_X86_REG_ESI,heap)
        u.mem_write(heap+0xB48,struct.pack('<3I',*row[2:5]));u.mem_write(bp-0x14,base.u32(row[6]))
        u.reg_write(UC_X86_REG_XMM0,row[5]);u.reg_write(UC_X86_REG_XMM1,row[7])
        u.emu_start(0x100ADB46,0x100ADB9C,count=40)
        position=bytes(u.mem_read(heap+0xB48,12));reset()
        expected.append(state+position+bytes(u.mem_read(0x1055959C,4)))
    payload=b''.join(struct.pack('<8I',*row) for row in rows)
    native=subprocess.run([str(runner)],input=payload,capture_output=True,check=True).stdout
    reference=b''.join(expected)
    if native!=reference:
        mismatch=next((i for i,(a,b) in enumerate(zip(native,reference)) if a!=b),None)
        raise AssertionError(dict(expected=len(reference),actual=len(native),firstByte=mismatch))
    receipt=dict(originalSha256=base.SHA,cases=len(rows),byteMismatches=0,originalKillCallbacks=len(kills),
        outputSha256=hashlib.sha256(native).hexdigest(),modeledServices=['KillLocal particle operation only'],
        boundary='Original scalar clamp/consumer and complete reset; no ambient movement bob, positive producer, actual host view integration or gameplay/performance acceptance')
    output.write_text(json.dumps(receipt,indent=2),'utf-8');return receipt
