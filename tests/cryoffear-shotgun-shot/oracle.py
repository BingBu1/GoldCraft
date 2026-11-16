# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the complete matched 870 PrimaryAttack and delayed shell callback."""
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


shared = load('shotgun_primary_abi', ROOT / 'tests/cryoffear-mp5-primary/oracle.py')
bits, number = shared.bits, shared.number
BASE, WORK, STACK, EDGE = shared.BASE, shared.WORK, shared.STACK, shared.EDGE


class Original(shared.MP5):
    def __init__(self, sample):
        super().__init__(sample)
        self.write(self.weapon + 0x168, 4)
        self.write(self.weapon + 0x1DC, 1)
        self.write(self.weapon + 0x200, 31)
        self.write(self.read(self.obj(1)) + 0x104, 0x1000D5A0)

    def snapshot(self):
        return [self.read(self.weapon + at) for at in (0x1E4, 0x1CC, 0x1D0, 0x1D8, 0x1B0, 0x1C8)] + \
            [self.read(self.pev(4) + 0x24C), self.read(self.pev(4) + 0x104),
             int(bool(self.read(self.weapon + 0x78))), self.read(self.obj(1) + 0x2294),
             self.read(self.obj(1) + 0x229C), self.read(self.pev(1) + 0x118), self.read(self.globals)]

    def advance(self):
        if self.mutation:
            self.write(self.globals, bits(number(self.read(self.globals)) + self.delta))

    def event(self, kind, *values):
        self.events += [kind, len(values) + 13, *values, *self.snapshot()]
        self.advance()
        if self.mutation == 2 and kind == 3:
            self.write(self.weapon + 0x1E4, int(not self.read(self.weapon + 0x1E4)))
            self.write(self.weapon + 0x1C8, 9)
            self.write(self.pev(4) + 0x24C, int(not self.read(self.pev(4) + 0x24C)))

    def hook(self, uc, address, size, user):
        self.visited.add(address)
        sp = uc.reg_read(UC_X86_REG_ESP)
        arg = lambda n: self.read(sp + 4 + n * 4)
        if address == EDGE + 0x168:
            assert (arg(0), arg(1)) == (0xFFFFFFFC, 4)
            self.event(32, arg(0), arg(1)); self.ret(0, self.recoil)
        elif address == 0x100EA240:
            label = bytes(uc.mem_read(arg(0), 32)).split(b'\0', 1)[0]
            assert label == b'shotgun_shoot'
            self.event(34); self.ret(8)
        elif address == 0x1000D5A0:
            self.event(41)  # Then execute the real eye-position instructions.
        elif address == 0x100DFEA0:
            self.event(35, arg(1))  # Then execute the real forward-copy helper.
        elif address == 0x10116A40:
            label = bytes(uc.mem_read(arg(0), 64)).split(b'\0', 1)[0]
            assert label == b'weapons_new/shotgun/shoot.wav'
            self.event(42, arg(1), arg(2), arg(7), arg(3), arg(4), arg(5)); self.ret(0)
        elif address == 0x10088CA0:
            self.event(31, arg(1), arg(12), arg(13), *[arg(i) for i in range(2, 12)], arg(14), arg(16))
            self.ret(68, arg(0))
        elif address == 0x100F3A40:
            label = bytes(uc.mem_read(arg(0), 32)).split(b'\0', 1)[0]
            assert label == b'!HEV_AMO0' and arg(1) == 0 and arg(2) == 0
            self.event(43); self.ret(12)
        elif address == EDGE + 0x6228:
            for i, value in enumerate((2, 3, 4)):
                self.write(arg(0) + i * 4, bits(value))
            self.advance(); self.ret(4, arg(0))
        else:
            super().hook(uc, address, size, user)

    def run(self, row):
        (op, now, clip, reserve, primary, secondary, idle, empty, action, special,
         gate, stage, abort, aim, infinite, water, pressed, damage, self.recoil,
         owner, seed, self.mutation, delta, pump, pending, scale) = row
        self.delta = number(delta)
        self.events, self.last, self.message = [], [], 0
        self.write(self.globals, now)
        for at, value in ((0x1CC, primary), (0x1D0, secondary), (0x1D8, idle), (0x1B0, empty),
                          (0x128, action), (0x140, special), (0x1E4, clip), (0x1F8, gate),
                          (0x1C8, stage), (0x178, aim), (0x78, 0x1006DC90 if pending else 0)):
            self.write(self.weapon + at, value)
        self.write(self.obj(1) + 0x2544, reserve)
        self.write(self.pev(4) + 0x24C, abort); self.write(self.pev(4) + 0x104, pump)
        self.write(self.obj(1) + 0x21E8, infinite << 16)
        self.write(self.obj(1) + 0x2814, owner); self.write(self.obj(1) + 0x2288, seed)
        self.write(self.obj(1) + 0x22B0, pressed); self.write(self.pev(1) + 0x1C4, water)
        self.write(self.pev(1) + 0x118, 8)
        self.write(self.obj(1) + 0x2294, 123); self.write(self.obj(1) + 0x229C, 456)
        self.write(0x1022685C, damage)
        for at, values in ((0x28, (1, 2, 3)), (0x34, (4, 5, 6)), (0x40, (7, 8, 9))):
            for i, value in enumerate(values):
                self.write(self.globals + at + i * 4, bits(value * number(scale)))
        for obj in (1, 4):
            for at, values in ((8, (11, 12, 13)), (0x178, (1, 2, 3)), (0x50, (4, 5, 6)),
                               (0x74, (1, 2, 3)), (0x68, (.1, .2, .3))):
                for i, value in enumerate(values):
                    self.write(self.pev(obj) + at + i * 4, bits(value))
        self.uc.reg_write(UC_X86_REG_ESP, STACK + 0xFF00)
        self.write(STACK + 0xFF00, EDGE + 0xFF00)
        self.uc.reg_write(UC_X86_REG_ECX, self.weapon)
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        self.uc.emu_start(0x1006DF20 if op == 0 else 0x1006DC90, EDGE + 0xFF00, count=20000)
        assert self.uc.reg_read(UC_X86_REG_EIP) == EDGE + 0xFF00
        return self.snapshot() + self.events


def generate(sample, proof, cases, expected):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(sample) != SHA:
        raise ValueError('Original shotgun sample mismatch')
    preedit = json.loads(proof.read_text('utf-8-sig'))
    if not preedit['beforeImplementation'] or preedit['sampleSha256'] != SHA:
        raise ValueError('Missing original pre-edit proof')
    mapper = load('shotgun_shot_mapper', ROOT / 'tests/cryoffear-magazine-actions/run.py')
    image = mapper.mapped_image(sample.read_bytes())
    covered = []
    for region in preedit['regions']:
        address, size = int(region['va'], 16), region['size']
        data = bytes.fromhex(region['hex'])
        if len(data) != size or image[address-BASE:address-BASE+size] != data or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Changed original/IDA region: ' + region['va'])
        covered.append((address, address + size))
    for name, digest in preedit['archives'].items():
        if sha(proof.parent / name) != digest:
            raise ValueError('Changed IDA archive: ' + name)
    machine = Original(sample); rng = random.Random(870105)
    counts = [0, 0]
    for op, count in ((0, 2048), (1, 256)):
        for i in range(count):
            now = bits(rng.choice((.1, 1.1, 13.37, 8192.3, 65536.125, 16777216)))
            row = [op, now, rng.choice((1, 2, 5, 75, 511, 10000)), rng.choice((0, 1, 50)), 0,
                   bits(number(now) + 1), 0, 0, 0, 0, 0, rng.choice((0, 1, 2)), rng.randrange(2),
                   rng.randrange(2), rng.randrange(2), 0, 1, bits(rng.choice((37, 61.05, 100.75))),
                   rng.randrange(-4, 5) & 0xFFFFFFFF, rng.randrange(3), rng.randrange(1 << 31),
                   i % 3, bits(.125), bits(-2), i % 2, bits(rng.choice((.01, .3, 1, 2.75, -1)))]
            branch = i % 16
            if branch == 0: row[4] = bits(number(now) + 1)
            elif branch == 1: row[8] = 1
            elif branch == 2: row[9] = 1
            elif branch == 3: row[15] = 3
            elif branch == 4: row[2] = 0
            elif branch == 5: row[2] = 0xFFFFFFFF
            elif branch == 6: row[2], row[7] = 0, now
            elif branch == 7: row[15], row[7] = 3, bits(number(now) + 1)
            elif branch == 8: row[16] = 0
            elif branch == 9: row[10], row[12] = 1, 0
            elif branch == 10: row[2], row[3], row[14] = 1, 0, 0
            cases.write(' '.join(f'{v:08x}' for v in row) + '\n')
            expected.write(' '.join(f'{v:08x}' for v in machine.run(row)) + '\n')
            counts[op] += 1
    controlled = {int(value, 16) for value in preedit['controlledOriginalEntries']}
    executed = {address for address in machine.visited if BASE <= address < BASE + len(image) and address not in controlled}
    uncovered = sorted(address for address in executed if not any(lo <= address < hi for lo, hi in covered))
    if uncovered:
        raise ValueError('Original instruction outside pre-edit proof: ' + ','.join(f'{v:08X}' for v in uncovered))
    return dict(sampleSha256=SHA, proofSha256=sha(proof), cases=sum(counts), operations=counts,
                wholeFunctions=['1006DF20', '1006DC90'], originalInstructions=len(executed),
                preeditCoverageComplete=True, actualHelpers=['1000D5A0', '100DFEA0'],
                controlledOriginalEntries=preedit['controlledOriginalEntries'],
                boundary='Complete 870 primary and pump-shell callback with actual eye/autoaim helpers; '
                         'controlled ballistics/audio/effects. No complete SDK weapon, deployment, gameplay or live performance.')
