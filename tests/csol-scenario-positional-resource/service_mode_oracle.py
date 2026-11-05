# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute original HW mode getter; the session-type virtual call is supplied.

The getter is a consumer of a separately supplied byte, not a map-ID inference.
No original DLL is loaded. IDA bytes and the complete module hash are pinned.
"""
from pathlib import Path
import hashlib
import json
import struct

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

HW_SHA = 'c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783'


def generate(hw, metadata, evidence, output):
    data = hw.read_bytes()
    assert hashlib.sha256(data).hexdigest() == HW_SHA
    meta = json.loads(metadata.read_text('utf-8-sig'))
    assert meta['sha256'] == HW_SHA

    def original(address, length):
        rva = address - int(meta['image_base'],16)
        for section in meta['sections']:
            if section['rva'] <= rva and rva + length <= section['rva'] + section['raw_size']:
                start = section['raw_offset'] + rva - section['rva']
                return data[start:start + length]
        raise ValueError(hex(address))

    path = evidence / 'round-service-mode-getter-before.json'
    saved = json.loads(path.read_text('utf-8-sig'))
    saved = saved.get('structuredContent') or json.loads(saved['content'][0]['text'])
    raw = bytes(int(x,16) for x in saved['result'][0]['data'].replace(',',' ').split())
    assert raw == original(0x37aa22b0,47)
    u = Uc(UC_ARCH_X86,UC_MODE_32)
    u.mem_map(0x37aa2000,0x1000)
    u.mem_write(0x37aa2000,original(0x37aa2000,0x1000))
    u.mem_map(0x500000,0x20000)
    u.mem_write(0x502000,struct.pack('<I',0x503000))
    u.mem_write(0x503240,struct.pack('<I',0x501000))
    session = 0

    def hook(mu,address,size,_):
        if address == 0x501000:
            sp = mu.reg_read(UC_X86_REG_ESP)
            target = struct.unpack('<I',mu.mem_read(sp,4))[0]
            mu.reg_write(UC_X86_REG_EAX,session)
            mu.reg_write(UC_X86_REG_ESP,sp+4)
            mu.reg_write(UC_X86_REG_EIP,target)

    u.hook_add(UC_HOOK_CODE,hook)
    observations = []
    for session in (0,1,2,255):
        for mode in range(256):
            u.mem_write(0x502352,bytes([mode]))
            u.mem_write(0x510000,struct.pack('<I',0x500000))
            u.reg_write(UC_X86_REG_ESP,0x510000)
            u.reg_write(UC_X86_REG_ECX,0x502000)
            u.emu_start(0x37aa22b0,0x500000,count=100)
            value = u.reg_read(UC_X86_REG_EAX)
            expected = {0:6,32:35}.get(mode,mode) if session == 1 else mode
            assert value == expected
            assert bytes(u.mem_read(0x502352,1)) == bytes([mode])
            observations.append(dict(sessionType=session,rawMode=mode,mode=value))
    result = dict(hwSha256=HW_SHA,ida=str(path.resolve()),address='37AA22B0',bytes=47,
                  cases=len(observations),failures=0,substitution='service vslot+240 returns explicit sessionType',
                  nativeDLLLoaded=False,liveGameplay=False,observations=observations)
    output.write_text(json.dumps(result,indent=2),'utf8')
    return result
