# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute complete original FAMAS PrimaryAttack and its actual autoaim/eye code."""
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


shared = load('famas_shot_abi', ROOT / 'tests/cryoffear-mp5-primary/oracle.py')
bits, number = shared.bits, shared.number
BASE, WORK, STACK, EDGE = shared.BASE, shared.WORK, shared.STACK, shared.EDGE


class FAMAS(shared.MP5):
    def __init__(self, sample):
        super().__init__(sample)
        self.write(self.weapon + 0x168, 23)
        self.write(self.weapon + 0x1F8, 31)
        self.write(self.read(self.obj(1)) + 0x104, 0x1000D5A0)

    def event(self, kind, *values):
        snapshot = [self.read(self.weapon + at) for at in (0x1E4, 0x19C, 0x1A0, 0x1D8, 0x1B0)]
        self.events += [kind, len(values) + len(snapshot), *values, *snapshot]

    def hook(self, uc, address, size, user):
        self.visited.add(address)
        sp = uc.reg_read(UC_X86_REG_ESP)
        arg = lambda n: self.read(sp + 4 + n * 4)
        if address == EDGE + 0x168:
            low, high = arg(0), arg(1)
            assert (low, high) in ((0xFFFFFFFE, 2),)
            self.event(32, low, high)
            self.ret(0, self.recoil)
        elif address == 0x100EA240:
            label = bytes(uc.mem_read(arg(0), 32)).split(b'\0', 1)[0]
            raise ValueError('FAMAS must not emit a named player fire action: ' + repr(label))
        elif address == 0x1000D5A0:
            self.event(41)  # Observe, then execute the real eye-position instructions.
        elif address == 0x10116A40:
            label = bytes(uc.mem_read(arg(0), 64)).split(b'\0', 1)[0]
            assert label == b'weapons_new/famas/famas_shoot.wav', label
            self.event(42, arg(1), arg(2), arg(7), arg(3), arg(4), arg(5))
            self.ret(0)
        else:
            super().hook(uc, address, size, user)

    def run(self, row):
        (now, clip, primary, action, special, water, empty, aim, infinite, flags,
         scalar, empty_deadline, damage, self.recoil, self.animation, owner, seed,
         increment, pressed, secondary) = row
        self.events, self.last, self.message = [], [], 0
        self.write(self.globals, now)
        for at, value in ((0x1CC, primary), (0x1D0, secondary), (0x1D8, 0), (0x1B0, empty_deadline),
            (0x128, action), (0x140, special), (0x1E4, clip), (0x1B8, empty), (0x178, aim),
            (0x19C, scalar), (0x198, increment), (0x1A0, 0), (0x1A8, 1), (0x1AC, 2)):
            self.write(self.weapon + at, value)
        self.write(self.obj(1) + 0x21E8, infinite << 16)
        self.write(self.obj(1) + 0x2814, owner); self.write(self.obj(1) + 0x2288, seed)
        self.write(self.obj(1) + 0x22B0, pressed)
        self.write(self.pev(1) + 0x1C4, water); self.write(self.pev(1) + 0x1A8, flags)
        self.write(self.pev(1) + 0x118, 8)
        self.write(self.obj(1) + 0x2298, 0); self.write(self.obj(1) + 0x22A0, 0)
        for at, values in ((0x28, (1, 2, 3)), (0x34, (4, 5, 6)), (0x40, (7, 8, 9))):
            for i, value in enumerate(values): self.write(self.globals + at + i * 4, bits(value))
        for obj in (1, 4):
            for at, values in ((8, (11, 12, 13)), (0x178, (1, 2, 3)), (0x50, (4, 5, 6)),
                (0x74, (1, 2, 3)), (0x68, (.1, .2, .3))):
                for i, value in enumerate(values): self.write(self.pev(obj) + at + i * 4, bits(value))
        for at in (0x18C, 0x190, 0x194): self.write(self.weapon + at, bits(.01))
        self.uc.reg_write(UC_X86_REG_ESP, STACK + 0xFF00)
        self.write(STACK + 0xFF00, EDGE + 0xFF00)
        self.uc.reg_write(UC_X86_REG_ECX, self.weapon)
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        self.uc.emu_start(0x1003BBD0, EDGE + 0xFF00, count=20000)
        assert self.uc.reg_read(UC_X86_REG_EIP) == EDGE + 0xFF00
        final = [self.read(self.weapon + at) for at in (0x1E4, 0x1CC, 0x1D0, 0x1D8, 0x1B0, 0x19C, 0x1A0)]
        final += [self.read(self.obj(1) + at) for at in (0x2298, 0x22A0)]
        return final + [self.read(self.pev(1) + 0x118)] + self.events


def generate(sample, proof, cases, expected):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(sample) != SHA: raise ValueError('Original CoF sample mismatch')
    preedit = json.loads(proof.read_text('utf-8-sig'))
    if not preedit['beforeImplementation'] or preedit['sampleSha256'] != SHA:
        raise ValueError('Original pre-edit proof mismatch')
    mapper = load('famas_shot_mapper', ROOT / 'tests/cryoffear-magazine-actions/run.py')
    image = mapper.mapped_image(sample.read_bytes())
    covered = []
    for region in preedit['regions']:
        address = int(region['va'], 16); data = bytes.fromhex(region['hex'])
        if not data or image[address-BASE:address-BASE+len(data)] != data or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Original instruction mismatch: ' + region['va'])
        covered.append((address, address + len(data)))
    for region in preedit['constants']:
        address = int(region['va'], 16); data = bytes.fromhex(region['hex'])
        if image[address-BASE:address-BASE+len(data)] != data or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Original constant mismatch: ' + region['va'])
    for name, digest in preedit['archives'].items():
        if sha(proof.parent / name) != digest: raise ValueError('Changed IDA evidence: ' + name)
    machine = FAMAS(sample); rng = random.Random(4326105); count = 1536
    for i in range(count):
        now = bits(rng.choice((.1, 1.1, 13.37, 4096.125, 8192.3, 65536.125)))
        row = [now, rng.choice((1, 2, 10, 75, 511, 10000)), 0, 0, 0, 0, 0, rng.randrange(2),
            rng.randrange(2), rng.choice((0, 0x4000)), bits(rng.random() * 3), 0,
            bits(15), rng.randrange(-2, 3) & 0xFFFFFFFF,
            rng.randrange(3), rng.randrange(3), rng.randrange(1 << 31),
            bits(rng.choice((.35, .75, 1.25))), 1, bits(number(now) + 1)]
        branch = i % 16
        if branch == 0: row[2] = bits(number(now) + 1)
        elif branch == 1: row[3] = 1
        elif branch == 2: row[4] = 1
        elif branch == 3: row[5] = 3
        elif branch == 4: row[1], row[6] = 0, 1
        elif branch == 5: row[1] = 0
        elif branch == 6: row[1], row[6], row[11] = 0, 1, now
        elif branch == 7: row[5], row[11] = 3, bits(number(now) + 1)
        elif branch == 8: row[18] = 0
        elif branch == 9: row[1], row[8] = 1, 0
        cases.write(' '.join(f'{v:08x}' for v in row) + '\n')
        expected.write(' '.join(f'{v:08x}' for v in machine.run(row)) + '\n')
    controlled = {int(address, 16) for address in preedit['controlledOriginalEntries']}
    executed = {address for address in machine.visited if BASE <= address < BASE + len(image) and address not in controlled}
    uncovered = sorted(address for address in executed if not any(start <= address < end for start, end in covered))
    if uncovered: raise ValueError('Executed original instructions outside pre-edit evidence: ' + ','.join(f'{v:08X}' for v in uncovered))
    return dict(sampleSha256=SHA, proofSha256=sha(proof), cases=count, originalFunction='1003BBD0',
        originalBytes=1770, originalInstructions=len(executed), preeditCoverageComplete=True,
        actualHelpers=['100DFEA0', '1000D5A0'], controlledOriginalEntries=preedit['controlledOriginalEntries'],
        boundary='Complete ordinary FAMAS primary, actual autoaim/eye-position instructions; controlled engine/damage/animation/audio ABI. No complete SDK weapon, deployment, gameplay or live performance.')
