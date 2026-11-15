# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the complete matched MP5 PrimaryAttack with controlled ABI consumers."""
import hashlib
import importlib.util
import json
from pathlib import Path
import random

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_MXCSR

ROOT = Path(__file__).resolve().parents[2]
SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


shared = load('mp5_primary_abi', ROOT / 'tests/cryoffear-tmp-weapon/oracle.py')
bits, number = shared.bits, shared.number
BASE, WORK, STACK, EDGE = shared.BASE, shared.WORK, shared.STACK, shared.EDGE


class MP5(shared.TMP):
    def __init__(self, sample):
        super().__init__(sample)
        self.write(self.weapon + 0x168, 27)
        self.write(self.weapon + 0x198, bits(.35))
        self.write(self.weapon + 0x1F8, 31)
        self.visited = set()

    def event(self, kind, *values):
        # Observe mutation order at each ABI consumer, not just final timers.
        snapshot = [self.read(self.weapon + off) for off in (0x1E4, 0x19C, 0x1A0, 0x1D8, 0x1B0)]
        return super().event(kind, *values, *snapshot)

    def hook(self, uc, address, size, user):
        self.visited.add(address)
        sp = uc.reg_read(UC_X86_REG_ESP)
        arg = lambda n: self.read(sp + 4 + n * 4)
        if address == EDGE + 0x168:
            low, high = arg(0), arg(1)
            self.event(32, low, high)
            self.ret(0, self.recoil if low == 0xFFFFFFFE else self.animation)
            return
        if address == EDGE + 0x71C0:
            self.event(40)
            self.ret(0)
            return
        if address == 0x100EA240:
            label = bytes(uc.mem_read(arg(0), 32)).split(b'\0', 1)[0]
            assert label == b'mp5_shoot', label
            self.event(34)
            self.ret(8)
            return
        if address == 0x100F91B0:
            self.event(8, arg(0), arg(1), arg(2))
            for offset, change in ((0x68, .25), (0x6C, -.5), (0x70, .75)):
                self.write(self.pev(1) + offset, bits(number(self.read(self.pev(1) + offset)) + change))
            self.ret(12)
            return
        if address == 0x100F2A50:
            self.event(33, arg(0))
            self.ret(4)
            return
        if address == 0x100DFEA0:
            self.event(35, arg(1))
            # Execute all 12 original instructions: forward copy, no side
            # effects or slope consumption. Do not use the older ABI stub.
            return
        return super().hook(uc, address, size, user)

    def run(self, row):
        (now, clip, primary, action, special, water, empty, aim, infinite, flags,
         scalar, empty_deadline, damage, self.recoil, self.animation, event_owner, seed, playing) = row
        self.events, self.last, self.message = [], [], 0
        self.write(self.globals, now)
        for off, value in ((0x1CC, primary), (0x1D0, 0), (0x1D8, 0), (0x1B0, empty_deadline),
                           (0x128, action), (0x140, special), (0x1E4, clip), (0x1B8, empty),
                           (0x178, aim), (0x19C, scalar), (0x1A0, 0), (0x1A8, 1), (0x1AC, 2)):
            self.write(self.weapon + off, value)
        self.uc.mem_write(self.weapon + 0x1A5, bytes([playing]))
        self.write(self.obj(1) + 0x21E8, infinite << 16)
        self.write(self.obj(1) + 0x2814, event_owner)
        self.write(self.obj(1) + 0x2288, seed)
        self.write(self.pev(1) + 0x1C4, water)
        self.write(self.pev(1) + 0x1A8, flags)
        self.write(self.pev(1) + 0x118, 8)
        self.write(self.obj(1) + 0x2298, 0)
        self.write(self.obj(1) + 0x22A0, 0)
        self.write(0x10226850, damage)
        for off, values in ((0x28, (1, 2, 3)), (0x34, (4, 5, 6)), (0x40, (7, 8, 9))):
            for i, value in enumerate(values):
                self.write(self.globals + off + i * 4, bits(value))
        for obj in (1, 4):
            for off, values in ((8, (11, 12, 13)), (0x178, (1, 2, 3)), (0x50, (4, 5, 6)),
                                (0x74, (1, 2, 3)), (0x68, (.1, .2, .3))):
                for i, value in enumerate(values):
                    self.write(self.pev(obj) + off + i * 4, bits(value))
        for off in (0x18C, 0x190, 0x194):
            self.write(self.weapon + off, bits(.01))
        self.uc.reg_write(UC_X86_REG_ESP, STACK + 0xFF00)
        self.write(STACK + 0xFF00, EDGE + 0xFF00)
        self.uc.reg_write(UC_X86_REG_ECX, self.weapon)
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        self.uc.emu_start(0x1005B400, EDGE + 0xFF00, count=20000)
        assert self.uc.reg_read(UC_X86_REG_EIP) == EDGE + 0xFF00
        final = [self.read(self.weapon + off) for off in (0x1E4, 0x1CC, 0x1D0, 0x1D8, 0x1B0, 0x19C, 0x1A0)]
        final += [self.read(self.obj(1) + off) for off in (0x2298, 0x22A0)]
        final += [self.read(self.pev(1) + 0x118), self.uc.mem_read(self.weapon + 0x1A5, 1)[0]]
        return final + self.events


def generate(sample, proof, cases, expected):
    if hashlib.sha256(sample.read_bytes()).hexdigest() != SHA:
        raise ValueError('Original MP sample mismatch')
    preedit = json.loads(proof.read_text('utf-8-sig'))
    if not preedit['beforeImplementation'] or preedit['sampleSha256'] != SHA:
        raise ValueError('Original IDA proof mismatch')
    mapper = load('mp5_primary_mapper', ROOT / 'tests/cryoffear-magazine-actions/run.py')
    image = mapper.mapped_image(sample.read_bytes())
    original = MP5(sample)
    region = next(v for v in preedit['evidence'] if int(v['va'], 16) == 0x1005B400)
    data = image[0x5B400:0x5B400 + region['bytes']]
    if len(data) != 1888 or hashlib.sha256(data).hexdigest() != region['sha256']:
        raise ValueError('Complete original primary region mismatch')
    archive = ROOT / Path(region['idaArchive'].replace('\\', '/'))
    if hashlib.sha256(archive.read_bytes()).hexdigest() != region['archiveSha256']:
        raise ValueError('Archived IDA evidence changed')
    rng = random.Random(527)
    count = 768
    for i in range(count):
        now = bits(rng.choice((.1, 1.1, 13.37, 4096.125, 8192.3, 65536.125)))
        row = [now, rng.choice((1, 2, 30, 75, 511, 10000)), 0, 0, 0, 0, 0, rng.randrange(2),
               rng.randrange(2), rng.choice((0, 0x4000)), bits(rng.random() * 3), 0,
               bits(rng.choice((37, 61.05, 100.75))), rng.randrange(-2, 3) & 0xFFFFFFFF,
               rng.randrange(3), rng.randrange(3), rng.randrange(1 << 31), rng.randrange(2)]
        branch = i % 12
        if branch == 0:
            row[2] = bits(number(now) + 1)
        elif branch == 1:
            row[3] = rng.choice((1, 2))
        elif branch == 2:
            row[4] = 1
        elif branch == 3:
            row[5] = 3
        elif branch == 4:
            row[1], row[6] = 0, 1
        elif branch == 5:
            row[1] = 0
        elif branch == 6:
            row[1], row[6], row[11] = 0, 1, now
        elif branch == 7:
            row[5], row[11] = 3, bits(number(now) + 1)
        elif branch == 8:
            row[1], row[6], row[7] = 0, 1, 1
        cases.write(' '.join(f'{v:08x}' for v in row) + '\n')
        expected.write(' '.join(f'{v:08x}' for v in original.run(row)) + '\n')
    return dict(sampleSha256=SHA, originalFunction='1005B400', bytes=1888, cases=count, originalAutoAim='100DFEA0',
                visitedInstructions=len(original.visited), proofSha256=hashlib.sha256(proof.read_bytes()).hexdigest(),
                boundary='Complete MP5 primary and original forward-copy autoaim; engine, animation, ballistics and audio ABI consumers controlled')
