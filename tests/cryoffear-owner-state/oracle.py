# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute original owner-state x86 clips with controlled owner/pev memory."""
import hashlib
import itertools
import random
import struct

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI,
    UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_MXCSR,
)

SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
BASE, OWNER, PEV, GLOBALS, STACK = 0x10000000, 0x40000000, 0x40004000, 0x40008000, 0x30000000


class Original:
    def __init__(self, sample):
        binary = sample.read_bytes()
        if hashlib.sha256(binary).hexdigest() != SHA:
            raise ValueError('Original sample identity differs')
        pe = struct.unpack_from('<I', binary, 0x3c)[0]
        if struct.unpack_from('<H', binary, pe + 4)[0] != 0x14c:
            raise ValueError('Expected original x86')
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(BASE, 0x300000)
        self.uc.mem_map(OWNER, 0x10000)
        self.uc.mem_map(STACK, 0x10000)
        section = pe + 24 + struct.unpack_from('<H', binary, pe + 20)[0]
        for index in range(struct.unpack_from('<H', binary, pe + 6)[0]):
            _, rva, size, raw = struct.unpack_from('<4I', binary, section + 40 * index + 8)
            if size:
                self.uc.mem_write(BASE + rva, binary[raw:raw + size])
        self.write(OWNER + 4, PEV)
        self.write(0x10223188, GLOBALS)
        self.uc.hook_add(UC_HOOK_CODE, self.hook)
        self.stops = set()
        self.visited = []

    def write(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def read(self, address):
        return struct.unpack('<I', self.uc.mem_read(address, 4))[0]

    def byte(self, address):
        return self.uc.mem_read(address, 1)[0]

    def hook(self, uc, address, size, context):
        if address in self.stops:
            self.visited.append(address)
            uc.emu_stop()

    def run(self, start, stops):
        self.stops = set(stops)
        self.visited = []
        for register, value in (
            (UC_X86_REG_ESP, STACK + 0x8000), (UC_X86_REG_EBP, STACK + 0x8000),
            (UC_X86_REG_ESI, OWNER), (UC_X86_REG_EDI, OWNER),
            (UC_X86_REG_ECX, OWNER), (UC_X86_REG_EDX, PEV), (UC_X86_REG_MXCSR, 0x1f80),
        ):
            self.uc.reg_write(register, value)
        self.uc.emu_start(start, 0, count=1000)
        if len(self.visited) != 1:
            raise ValueError(f'Original clip did not reach its boundary: {start:x}')
        return self.visited[0]


def generate(sample, output):
    original = Original(sample)
    randomizer = random.Random(0xc0f0)
    counts = [0] * 4

    def emit(kind, *values):
        row = [kind, *(value & 0xffffffff for value in values)]
        if len(row) > 12:
            raise ValueError('Oracle record is too large')
        output.write(' '.join(f'{value:x}' for value in row + [0] * (12 - len(row))) + '\n')
        counts[kind] += 1

    edges = [(randomizer.getrandbits(32), randomizer.getrandbits(32)) for _ in range(512)]
    sequence = [0, 2, 2, 0, 2, 6, 4, 0, 0xffffffff, 0xffffffff, 0, 2]
    edges.extend(zip(sequence, sequence[1:]))
    for previous, current in edges:
        original.write(OWNER + 0x22ac, previous)
        original.write(PEV + 0x184, current)
        original.run(0x100efcba, [0x100efce2])
        emit(0, previous, current, original.read(OWNER + 0x22b0), original.read(OWNER + 0x22b4))

    for phone, waterjump, level, water_type, pressed, grounded, ground_entity in itertools.product(
        [0, 1, 255], [0, 0x800], [-0x80000000, -2, 0, 1, 2, 3, 0x7fffffff],
        [-19, -3, 0], [0, 2, 3, 0xffffffff], [0, 0x200], [0, 1],
    ):
        flags = waterjump | grounded | 0x40000
        original.uc.mem_write(OWNER + 0x1ee8, bytes([phone]))
        original.write(PEV + 0x1a8, flags)
        original.write(PEV + 0x1c4, level)
        original.write(PEV + 0x1c8, water_type)
        original.write(OWNER + 0x22b0, pressed)
        original.write(PEV + 0x1a0, ground_entity)
        accepted = original.run(0x100e5030, [0x100e509b, 0x100e528e]) == 0x100e509b
        emit(1, phone, flags, level, water_type, pressed, ground_entity, accepted)

    times = [0, 0x80000000, 0x3f800000, 0xbf800000, 0x4b800000, 0x4b800001,
             0xcb800000, 0x7f800000, 0xff800000, 0x7fc12345, 0xffc23456]
    times.extend(randomizer.getrandbits(32) for _ in range(256))
    for now in times:
        original.uc.mem_write(OWNER + 0x3aa, bytes([255]))
        original.write(OWNER + 0x3ac, 0xdeadbeef)
        original.write(GLOBALS, now)
        original.run(0x100e520f, [0x100e522f])
        emit(2, 255, 0xdeadbeef, now, original.byte(OWNER + 0x3aa), original.read(OWNER + 0x3ac))

    deadlines = [0, 0x40400000, 0x7fc12345, 0x7f800000, 0xff800000]
    clear_times = [0, 0x40000000, 0x40400000, 0x40400001, 0x40800000, 0x7fc23456,
                   0x7f800000, 0xff800000]
    for flag, grounded, now, deadline in itertools.product([0, 1, 2, 255], [0, 0x200], clear_times, deadlines):
        original.uc.mem_write(OWNER + 0x3aa, bytes([flag]))
        original.write(OWNER + 0x3ac, deadline)
        original.write(PEV + 0x1a8, grounded)
        original.write(GLOBALS, now)
        original.run(0x100eb22d, [0x100eb25e])
        emit(3, flag, deadline, grounded, now, original.byte(OWNER + 0x3aa), original.read(OWNER + 0x3ac))

    return dict(moduleSha256=SHA, cases=sum(counts), counts=counts,
                boundary='Unmodified x86 clips with controlled memory; no full Jump, lifecycle, sprint, gameplay or performance claim')
