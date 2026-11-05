# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded, unchanged original MP selector/scanner and HW table getters.

No DLL is loaded. The MP service getter and CRT/security endpoints are explicit
substitutions. Original code bytes must match saved IDA responses first.
"""
from pathlib import Path
import ctypes
import hashlib
import importlib.util
import json
import struct

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

MP_SHA = '39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2'
HW_SHA = 'c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783'


def identity(path):
    path = Path(path).resolve(strict=True)
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class Original:
    def __init__(self, mp, evidence):
        self.input = identity(mp)
        assert self.input['sha256'] == MP_SHA
        data = Path(mp).read_bytes()
        nt = struct.unpack_from('<I', data, 0x3c)[0]
        assert struct.unpack_from('<H', data, nt + 4)[0] == 0x14c
        optional = nt + 24
        self.base = struct.unpack_from('<I', data, optional + 28)[0]
        size = struct.unpack_from('<I', data, optional + 56)[0]
        assert self.base == 0x10000000
        self.sections = []
        count = struct.unpack_from('<H', data, nt + 6)[0]
        headers = optional + struct.unpack_from('<H', data, nt + 20)[0]
        for i in range(count):
            _, rva, length, offset = struct.unpack_from('<4I', data, headers + i * 40 + 8)
            self.sections.append((rva, data[offset:offset + length]))
        self.verified = []
        for name, start, end in (
                ('round-service-selector', 0x1138e24d, 0x1138e281),
                ('round-list-scanner', 0x11391ef0, 0x11391f6c),
                ('rounds-loader-after', 0x1138e0c0, 0x1138e7a7)):
            path = evidence / (name + '-ida-bytes.json')
            result = json.loads(path.read_text('utf-8-sig'))
            result = result.get('structuredContent') or json.loads(result['content'][0]['text'])
            raw = bytes(int(v, 16) for v in result['result'][0]['data'].replace(',', ' ').split())
            assert raw == self.original(start, end - start)
            self.verified.append(dict(ida=identity(path), start=hex(start), end=hex(end),
                                      sha256=hashlib.sha256(raw).hexdigest()))
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        self.u.mem_map(self.base, (size + 4095) & ~4095)
        for rva, raw in self.sections:
            self.u.mem_write(self.base + rva, raw)
        self.u.mem_map(0x500000, 0x20000)
        self.u.mem_map(0x600000, 0x10000)
        self.u.mem_write(0x117402bc, struct.pack('<I', 0x501000))
        self.crt = ctypes.CDLL('ucrtbase.dll')
        self.crt.atoi.argtypes = [ctypes.c_char_p]
        self.crt.atoi.restype = ctypes.c_int
        self.failed = False
        self.u.hook_add(UC_HOOK_CODE, self.hook)

    def original(self, address, size):
        for rva, raw in self.sections:
            if rva <= address - self.base < rva + len(raw):
                offset = address - self.base - rva
                result = raw[offset:offset + size]
                assert len(result) == size
                return result
        raise ValueError(hex(address))

    def hook(self, u, address, size, _):
        if address == 0x1138e262:
            # Second lookup of the same explicit service-mode value. Do not
            # emulate unrelated manager objects or pretend this is a Session.
            u.reg_write(UC_X86_REG_EIP, 0x1138e272)
        elif address in (0x501000, 0x115907ca):
            sp = u.reg_read(UC_X86_REG_ESP)
            destination, pointer = struct.unpack('<II', u.mem_read(sp, 8))
            if address == 0x501000:
                raw = bytes(u.mem_read(pointer, 12)).split(b'\0', 1)[0]
                assert len(raw) < 10 and raw.isdigit()
                u.reg_write(UC_X86_REG_EAX, self.crt.atoi(raw) & 0xffffffff)
            u.reg_write(UC_X86_REG_ESP, sp + 4)
            u.reg_write(UC_X86_REG_EIP, destination)
        elif address == 0x11590ec2:
            self.failed = True
            u.emu_stop()

    def start(self, mode):
        self.u.reg_write(UC_X86_REG_EAX, mode & 0xffffffff)
        self.u.reg_write(UC_X86_REG_EFLAGS, 2)
        self.u.emu_start(0x1138e25d, 0x1138e281, count=30)
        return self.u.reg_read(UC_X86_REG_EBX)

    def scan(self, text):
        assert len(text) < 300 and '\0' not in text
        self.failed = False
        self.u.mem_write(0x502000, text.encode('ascii') + b'\0')
        self.u.mem_write(0x503000, struct.pack('<I', 77))
        self.u.mem_write(0x504000, bytes(4096))
        self.u.mem_write(0x608000, struct.pack('<4I', 0x500000, 0x502000, 0x503000, 0x504000))
        self.u.reg_write(UC_X86_REG_ESP, 0x608000)
        self.u.reg_write(UC_X86_REG_EBP, 0x608100)
        self.u.emu_start(0x11391ef0, 0x500000, count=10000)
        if self.failed:
            return None
        count = struct.unpack('<I', self.u.mem_read(0x503000, 4))[0]
        assert count < 100
        results = []
        for value in struct.unpack('<' + 'I' * count, self.u.mem_read(0x504000, count * 4)):
            # Execute the actual round loader low-word read rather than use
            # the production conversion to form the expected result.
            self.u.reg_write(UC_X86_REG_EBP, 0x608100)
            self.u.reg_write(UC_X86_REG_EAX, 0)
            self.u.mem_write(0x608100 - 0x1cc, struct.pack('<I', value))
            self.u.emu_start(0x1138e450, 0x1138e458, count=2)
            results.append(self.u.reg_read(UC_X86_REG_EDX))
        return results
