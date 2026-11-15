# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute complete original M16 Shoot and its real delay/autoaim helpers."""
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


mp5 = load('m16_shot_shared', ROOT / 'tests/cryoffear-mp5-primary/oracle.py')
bits, number = mp5.bits, mp5.number
BASE, WORK, STACK, EDGE = mp5.BASE, mp5.WORK, mp5.STACK, mp5.EDGE


class M16(mp5.MP5):
    def __init__(self, sample):
        super().__init__(sample)
        self.write(self.weapon + 0x168, 9)
        self.write(self.weapon + 0x1FC, 31)
        self.write(WORK + 0xA0000 + 8, 0x1011F680)

    def event(self, kind, *values):
        snapshot = [self.read(self.weapon + at) for at in (0x1E4, 0x19C, 0x1A0, 0x1D8, 0x1B0)]
        snapshot += [self.read(self.weapon + 0x1F8), int(self.read(self.weapon + 0x78) == 0x10052120),
                     self.read(self.weapon + 0x58)]
        self.events += [kind, len(values) + len(snapshot), *values, *snapshot]

    def hook(self, uc, address, size, user):
        sp = uc.reg_read(UC_X86_REG_ESP)
        arg = lambda n: self.read(sp + 4 + n * 4)
        if address == EDGE + 0x168:
            assert (arg(0), arg(1)) == (0, 2)
            self.event(32, arg(0), arg(1))
            self.ret(0, self.recoil)
        elif address == 0x100EA240:
            label = bytes(uc.mem_read(arg(0), 32)).split(b'\0', 1)[0]
            assert label == b'm16_shoot', label
            self.event(34)
            self.ret(8)
        elif address == EDGE + 0x6104:
            self.event(41)
            super().hook(uc, address, size, user)
        elif address == 0x10116A40:
            label = bytes(uc.mem_read(arg(0), 64)).split(b'\0', 1)[0]
            assert label == b'weapons_new/m16/m16_fire.wav', label
            self.event(42, arg(1), arg(2), arg(7), arg(3), arg(4), arg(5))
            self.ret(0)
        else:
            super().hook(uc, address, size, user)

    def run(self, row):
        (now, clip, primary, action, special, water, empty, aim, infinite, flags,
         scalar, empty_deadline, damage, self.recoil, owner, seed, burst, shots,
         increment, deadline, pending) = row
        self.events, self.last, self.message = [], [], 0
        self.write(self.globals, now)
        for at, value in ((0x1CC, primary), (0x1D0, 0), (0x1D8, 0), (0x1B0, empty_deadline),
            (0x128, action), (0x140, special), (0x1E4, clip), (0x1B8, empty), (0x178, aim),
            (0x19C, scalar), (0x198, increment), (0x1A0, 0), (0x1A8, 1), (0x1AC, 2),
            (0x1F8, shots), (0x200, burst), (0x58, deadline), (0x78, 0x10052120 if pending else 0)):
            self.write(self.weapon + at, value)
        self.write(self.pev(4) + 0x104, deadline)
        self.write(self.obj(1) + 0x21E8, infinite << 16)
        self.write(self.obj(1) + 0x2814, owner)
        self.write(self.obj(1) + 0x2288, seed)
        self.write(self.pev(1) + 0x1C4, water)
        self.write(self.pev(1) + 0x1A8, flags)
        self.write(self.pev(1) + 0x118, 8)
        self.write(self.obj(1) + 0x2298, 0)
        self.write(self.obj(1) + 0x22A0, 0)
        self.write(0x10226854, damage)
        for at, values in ((0x28, (1, 2, 3)), (0x34, (4, 5, 6)), (0x40, (7, 8, 9))):
            for i, value in enumerate(values):
                self.write(self.globals + at + i * 4, bits(value))
        for obj in (1, 4):
            for at, values in ((8, (11, 12, 13)), (0x178, (1, 2, 3)), (0x50, (4, 5, 6)),
                (0x74, (1, 2, 3)), (0x68, (.1, .2, .3))):
                for i, value in enumerate(values):
                    self.write(self.pev(obj) + at + i * 4, bits(value))
        for at in (0x18C, 0x190, 0x194):
            self.write(self.weapon + at, bits(.01))
        self.uc.reg_write(UC_X86_REG_ESP, STACK + 0xFF00)
        self.write(STACK + 0xFF00, EDGE + 0xFF00)
        self.uc.reg_write(UC_X86_REG_ECX, self.weapon)
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        self.uc.emu_start(0x10052120, EDGE + 0xFF00, count=20000)
        assert self.uc.reg_read(UC_X86_REG_EIP) == EDGE + 0xFF00
        final = [self.read(self.weapon + at) for at in (0x1E4, 0x1CC, 0x1D0, 0x1D8, 0x1B0, 0x19C, 0x1A0)]
        final += [self.read(self.obj(1) + at) for at in (0x2298, 0x22A0)]
        final += [self.read(self.pev(1) + 0x118), self.read(self.weapon + 0x1F8),
            int(self.read(self.weapon + 0x78) == 0x10052120), self.read(self.weapon + 0x58),
            self.read(self.pev(4) + 0x104)]
        return final + self.events


def generate(sample, proof, cases, expected):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(sample) != SHA:
        raise ValueError('Original sample mismatch')
    preedit = json.loads(proof.read_text('utf-8-sig'))
    if not preedit['beforeImplementation'] or preedit['sampleSha256'] != SHA:
        raise ValueError('Original IDA proof mismatch')
    mapper = load('m16_shot_mapper', ROOT / 'tests/cryoffear-magazine-actions/run.py')
    image = mapper.mapped_image(sample.read_bytes())
    for region in preedit['regions']:
        data = bytes.fromhex(region['hex'])
        at = int(region['va'], 16) - BASE
        if image[at:at + len(data)] != data or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Original instruction mismatch')
    for name, digest in preedit['archives'].items():
        if sha(proof.parent / name) != digest:
            raise ValueError('Archived IDA evidence changed')
    machine = M16(sample)
    rng = random.Random(521)
    count = 1024
    for i in range(count):
        now = bits(rng.choice((.1, 1.1, 13.37, 4096.125, 8192.3, 65536.125)))
        row = [now, rng.choice((1, 2, 20, 75, 511, 10000)), 0, 0, 0, 0, 0, rng.randrange(2),
            rng.randrange(2), rng.choice((0, 0x4000)), bits(rng.random() * 3), 0,
            bits(rng.choice((37, 61.05, 100.75))), rng.randrange(3), rng.randrange(3),
            rng.randrange(1 << 31), rng.randrange(2), rng.choice((0, 1, 2, 3)),
            bits(rng.choice((.35, .75, 1.25))), bits(.5), rng.randrange(2)]
        branch = i % 12
        if branch == 0:
            row[2] = bits(number(now) + 1)  # Scheduled shot ignores group cooldown.
        elif branch == 1:
            row[3] = 1
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
        cases.write(' '.join(f'{v:08x}' for v in row) + '\n')
        expected.write(' '.join(f'{v:08x}' for v in machine.run(row)) + '\n')
    return dict(sampleSha256=SHA, proofSha256=sha(proof), cases=count, originalFunction='10052120',
        originalBytes=1720, actualHelpers=['100DFEA0', '1011F680', '1011AB20'],
        visitedInstructions=len(machine.visited),
        boundary='Complete original Shoot, real .05F scheduling and forward-copy autoaim. Controlled engine, animation, bullet and sound ABI; no actual owned-think dispatch, SDK admission, deployment, gameplay or performance acceptance.')
