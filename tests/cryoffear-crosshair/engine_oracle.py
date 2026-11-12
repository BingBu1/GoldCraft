#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the matching original CoF engine's actual float delta truncation."""
import hashlib
import json
import random
import struct
import subprocess
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBP,UC_X86_REG_ESP,UC_X86_REG_FPCW,UC_X86_REG_MXCSR

SHA='9dd34e536c4bb7cda3bc1bc4f0f5f6163687566a16f35c0ea0be59f93da62875'


def verify(sample,runner,output):
    image=sample.read_bytes()
    if hashlib.sha256(image).hexdigest()!=SHA:raise ValueError('Unreviewed original engine')
    pe=struct.unpack_from('<I',image,60)[0];optional=pe+24
    base=struct.unpack_from('<I',image,optional+28)[0]
    size=struct.unpack_from('<I',image,optional+56)[0]
    uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(base,(size+4095)&~4095)
    uc.mem_write(base,image[:struct.unpack_from('<I',image,optional+60)[0]])
    sections=optional+struct.unpack_from('<H',image,pe+20)[0]
    for i in range(struct.unpack_from('<H',image,pe+6)[0]):
        at=sections+i*40;rva,count,raw=struct.unpack_from('<III',image,at+12)
        if count:uc.mem_write(base+rva,image[raw:raw+count])
    data,stack=0x30000000,0x40000000
    uc.mem_map(data,4096);uc.mem_map(stack,8192)
    uc.reg_write(UC_X86_REG_FPCW,0x27f);uc.reg_write(UC_X86_REG_MXCSR,0x1f80)
    cpu=uc.context_save();rows=[]
    for whole in range(11):
        for fraction in (0.0,0.000001,0.25,0.5,0.75,0.999999):
            if whole+fraction<=10:rows.append(whole+fraction)
    rows.extend([0.0,-0.0,1e-45,1e-35,9.999999,10.0])
    rng=random.Random(100195);rows.extend(rng.uniform(0,10) for _ in range(2048))
    binary=b''.join(struct.pack('<f',value) for value in rows);expected=bytearray()
    for at in range(0,len(binary),4):
        uc.context_restore(cpu)
        uc.reg_write(UC_X86_REG_EAX,data)
        uc.reg_write(UC_X86_REG_EBP,stack+4096);uc.reg_write(UC_X86_REG_ESP,stack+2048)
        uc.mem_write(data+48,struct.pack('<f',1.0))
        uc.mem_write(stack+4096-8,binary[at:at+4])
        # Original DT_FLOAT branch: fld input; fmul premultiply; fst float
        # scratch; call actual rounding-mode-controlled _ftol; save int32.
        uc.emu_start(0x1d50427,0x1d50438,count=100)
        expected.extend(uc.mem_read(stack+4096-48,4))
    run=subprocess.run([str(runner),'--quantize'],input=binary,capture_output=True,timeout=30)
    run.check_returncode()
    if run.stdout!=expected:raise AssertionError('Original engine quantization differs')
    result=dict(success=True,cases=len(binary)//4,originalSha256=SHA,
        stage='1D50427..1D50438 including actual1E7163C _ftol, delta pre=1',
        originalBytesSha256=hashlib.sha256(uc.mem_read(0x1d50427,17)).hexdigest(),
        outputSha256=hashlib.sha256(run.stdout).hexdigest(),
        boundary='Reachable finite original Glock0..10; signed10-bit output is within range, reader pre/post1 leaves these integers unchanged')
    output.write_text(json.dumps(result,indent=2),'utf8');return result
