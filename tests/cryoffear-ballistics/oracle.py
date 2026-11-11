# SPDX-License-Identifier: GPL-3.0-or-later
"""Private original machine-code oracle; controlled engine/entity boundaries."""
import hashlib
import random
import struct

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_MXCSR

SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
BASE, WORK, STACK, EDGE = 0x10000000, 0x40000000, 0x30000000, 0x50000000


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def number(value):
    return struct.unpack('<f', struct.pack('<I', value))[0]


def f32(value):
    return number(bits(value))


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
        n, opt = struct.unpack_from('<H', binary, pe + 6)[0], pe + 24
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0]
        for i in range(n):
            _, rva, size, raw = struct.unpack_from('<4I', binary, section + 40 * i + 8)
            if size:
                self.uc.mem_write(BASE + rva, binary[raw:raw + size])
        self.uc.mem_map(WORK, 0x100000)
        self.uc.mem_map(STACK, 0x10000)
        self.uc.mem_map(EDGE, 0x10000)
        self.globals = WORK + 0x80000
        self.write(0x10223188, self.globals)
        self.write(self.globals + 0x98, WORK + 0xb0000)  # Empty controlled classname table.
        self.write(0x102328EC, 0)  # Original baseline float-to-uint path, no AVX512.
        self.weapon = WORK + 0x90000
        self.write(self.obj(1) + 0x252c, self.weapon)
        self.write(0x10226558, 77)
        self.write(0x10231F1C, 13)
        for index in range(6):
            self.write(self.obj(index), self.vtable(index))
            self.write(self.obj(index) + 4, self.pev(index))
            self.write(self.pev(index) + 0x20c, self.edict(index))
            self.write(self.pev(index) + 0x10c, 4 if index == 0 else 2)
            self.write(self.edict(index) + 0x7c, self.obj(index))
            for offset in (0x60, 0x64, 0x78, 0xc0, 0xd0, 0x138):
                self.write(self.vtable(index) + offset, EDGE + 0x2000 + index * 0x200 + offset)
        for offset in (0x7c, 0xb4, 0xb8, 0xbc, 0xc0, 0xc8, 0xd4, 0xd8, 0xdc, 0x11c):
            self.write(0x10222F48 + offset, EDGE + offset)
        self.uc.hook_add(UC_HOOK_CODE, self.hook)
        self.visited = set()

    @staticmethod
    def obj(index): return WORK + index * 0x10000
    @staticmethod
    def pev(index): return WORK + index * 0x10000 + 0x4080
    @staticmethod
    def edict(index): return WORK + index * 0x10000 + 0x4000
    @staticmethod
    def vtable(index): return WORK + index * 0x10000 + 0x5000

    def write(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def read(self, address):
        return struct.unpack('<I', self.uc.mem_read(address, 4))[0]

    def vector(self, address):
        return [self.read(address + i * 4) for i in range(3)]

    def index(self, address, member):
        return next(i for i in range(6) if address == member(i))

    def event(self, kind, *values):
        self.events += [kind, len(values), *values]

    def ret(self, count, value=0):
        stack = self.uc.reg_read(UC_X86_REG_ESP)
        target = self.read(stack)
        self.uc.reg_write(UC_X86_REG_ESP, stack + 4 + count)
        self.uc.reg_write(UC_X86_REG_EAX, value)
        self.uc.reg_write(UC_X86_REG_EIP, target)

    def hook(self, uc, address, size, user):
        self.last = (self.last + [address])[-16:]
        if BASE <= address < BASE + 0x300000:
            self.visited.add(address)
            if address == 0x1011c070:
                self.event(1)
            elif address == 0x1011b8f0:
                sp = uc.reg_read(UC_X86_REG_ESP)
                self.event(5, self.index(self.read(sp + 4), self.pev), self.index(self.read(sp + 8), self.pev))
            return
        if not EDGE <= address < EDGE + 0x10000:
            raise ValueError(f'Unexpected original callback {address:08x}')
        sp = uc.reg_read(UC_X86_REG_ESP)
        arg = lambda n: self.read(sp + 4 + n * 4)
        if address >= EDGE + 0x2000:
            entity, offset = divmod(address - EDGE - 0x2000, 0x200)
            if offset == 0xd0:
                self.ret(0, int(entity == 1 or 2 <= entity < 2 + self.players))
            elif offset == 0x78:
                self.ret(0, self.obj(entity) if entity != 0 else 0)
            elif offset == 0xc0:
                self.ret(0, int(entity == 0))
            elif offset == 0x138:
                self.ret(4, 1)
            elif offset == 0x60:
                self.event(3, entity, self.index(arg(0), self.pev), arg(1), arg(2), arg(3), arg(4), self.read(arg(5) + 0x10), arg(6))
                self.write(0x10231f34, self.obj(entity))
                self.write(0x10231f38, arg(1))
                self.ret(28)
            elif offset == 0x64:
                self.event(4, entity, self.index(arg(0), self.pev), self.index(arg(1), self.pev), arg(2), arg(3))
                self.ret(16, 1)
            else:
                raise ValueError(f'Unknown virtual edge {offset:x}')
        elif address == EDGE + 0x7c:  # TraceLine
            start, end, _, skip, trace = [arg(i) for i in range(5)]
            # Original retraces pass trace.vecEndPos as input while reusing trace.
            # Snapshot engine arguments before writing the output TraceResult.
            start_values, end_values = self.vector(start), self.vector(end)
            self.event(2, self.index(skip, self.edict), *start_values, *end_values)
            self.trace_count += 1
            player = self.trace_count <= self.players
            target = self.trace_count + 1 if player else (0 if self.final == 0 else 5)
            hit = player or self.final != 2
            fraction = .5 if hit else 1.0
            self.uc.mem_write(trace, bytes(56))
            self.write(trace + 0xc, int(self.water == 2))
            self.write(trace + 0x10, bits(fraction))
            for i in range(3):
                a, b = number(start_values[i]), number(end_values[i])
                self.write(trace + 0x14 + i * 4, bits(f32(a + f32(f32(b - a) * fraction))))
            self.write(trace + 0x2c, bits(1))
            self.write(trace + 0x30, self.edict(target))
            self.ret(0)
        elif address == EDGE + 0xb4:  # PointContents
            position = self.vector(arg(0))
            self.event(12, *position)
            z = number(position[2])
            water = self.water != 0 and z < self.surface
            self.ret(0, -3 if water else -1)
        elif address == EDGE + 0xb8:
            self.event(6, arg(0), arg(1))
            self.ret(0)
        elif address == EDGE + 0xbc:
            self.event(11)
            self.ret(0)
        elif address == EDGE + 0xc0:
            self.event(8, arg(0))
            self.ret(0)
        elif address == EDGE + 0xc8:
            self.event(9, arg(0))
            self.ret(0)
        elif address == EDGE + 0xd4:
            self.event(7, arg(0))
            self.ret(0)
        elif address == EDGE + 0xd8:
            text = bytes(self.uc.mem_read(arg(0), 5))
            if text != b'shot\0':
                raise ValueError('Unexpected original impact string')
            self.event(10, 1)
            self.ret(0)
        elif address == EDGE + 0x11c:
            self.ret(0, self.index(arg(0), self.edict))
        elif address == EDGE + 0xdc:
            self.ret(0, self.edict(0))
        else:
            raise ValueError(f'Unexpected engine edge {address - EDGE:x}')

    def run(self, row):
        self.events, self.trace_count, self.last = [], 0, []
        self.players, self.final, self.water = row[18:21]
        self.surface = number(row[21])
        for i, value in enumerate(row[9:12]):
            self.write(self.globals + 0x40 + 4 * i, value)
        for i, value in enumerate(row[12:15]):
            self.write(self.globals + 0x34 + 4 * i, value)
        self.uc.mem_write(self.obj(1) + 0x390, bytes(8))
        self.uc.mem_write(self.weapon + 0x178, bytes([row[17]]))
        stack, result = STACK + 0x8000, WORK + 0xa0000
        args = [0x60000000, result, 1, *row[:9], bits(8192), 3, 2, row[16], self.pev(1), row[15]]
        for i, value in enumerate(args):
            self.write(stack + 4 * i, value)
        self.uc.reg_write(UC_X86_REG_ESP, stack)
        self.uc.reg_write(UC_X86_REG_ECX, self.obj(1))
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        try:
            self.uc.emu_start(0x10088ca0, 0x60000000, count=20000)
        except Exception as error:
            raise RuntimeError(f'Original stopped at {self.uc.reg_read(UC_X86_REG_EIP):08x}, last={[hex(x) for x in self.last]}, row={row}, events={self.events[-30:]}') from error
        if self.uc.reg_read(UC_X86_REG_EIP) != 0x60000000:
            raise ValueError('Original full bullet function failed to return')
        return self.vector(result), self.events


def generate(sample, cases_file, expected_file):
    original = Original(sample)
    randomizer = random.Random(0x10088ca0)
    count = 0
    for i in range(512):
        row = [bits(randomizer.uniform(-128, 128)) for _ in range(3)]
        row += [bits(randomizer.uniform(-1, 1)) for _ in range(3)]
        row += [bits(randomizer.uniform(0, .1)) for _ in range(3)]
        row += [bits(randomizer.uniform(-1, 1)) for _ in range(6)]
        row += [randomizer.getrandbits(32), (0, 1, 15, 16, 17, 35, 100, 1000)[i % 8], i % 2,
                (i // 2) % 5, (i // 10) % 3, (i // 30) % 3, bits(randomizer.uniform(-100, 200))]
        result, events = original.run(row)
        cases_file.write(' '.join(f'{value:08x}' for value in row) + '\n')
        expected_file.write(' '.join(f'{value:08x}' for value in [*result, *events]) + '\n')
        count += 1
    required = (0x10088d1e, 0x10088dd6, 0x1008909b, 0x100890cf, 0x10089135, 0x1008919c,
                0x1008936c, 0x1008943c, 0x10117c78, 0x1011b919)
    if not all(address in original.visited for address in required):
        raise ValueError('Missing critical original branch coverage')
    return dict(sampleSha256=SHA, functionVA='10088CA0', functionRVA='88CA0', cases=count,
                visitedInstructions=len(original.visited), requiredInstructions=[f'{x:08x}' for x in required],
                boundary='Original full bullet/helper instructions; controlled SDK/entity callbacks, no live engine')
