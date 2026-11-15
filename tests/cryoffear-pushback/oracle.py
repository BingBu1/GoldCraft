# SPDX-License-Identifier: GPL-3.0-or-later
"""Complete original player MeleePushBack with explicit engine/target edges."""
import hashlib
import importlib.util
import json
from pathlib import Path
import random

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('pushback_shared', ROOT / 'tests/cryoffear-ballistics/oracle.py')
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
BASE, WORK, STACK, EDGE = shared.BASE, shared.WORK, shared.STACK, shared.EDGE
bits = shared.bits


class Pushback(shared.Original):
    def __init__(self, sample):
        super().__init__(sample)
        self.write(0x10222F88, EDGE + 0x40)
        self.write(0x10222F94, EDGE + 0x4C)
        self.write(0x10223060, EDGE + 0x118)
        self.write(self.vtable(1) + 0x228, EDGE + 0x6228)
        for i in range(6):
            self.write(self.vtable(i) + 0xBC, 0x10013550)

    def event(self, kind, *values):
        values = (*values, self.read(self.obj(1) + 0x21F0))
        self.events += [kind, len(values), *values]

    def mutate(self, stage):
        if self.mutation == stage:
            self.write(self.obj(1) + 0x21F0, self.new_stamina)
            self.uc.mem_write(self.obj(1) + 0x21E8, bytes((self.new_infinite,)))

    def hook(self, uc, address, size, user):
        if BASE <= address < BASE + 0x300000:
            self.visited.add(address)
        sp = uc.reg_read(UC_X86_REG_ESP)
        arg = lambda n: self.read(sp + 4 + n * 4)
        owner = self.obj(1)
        if address == 0x100FC9F0:
            sample = bytes(uc.mem_read(arg(2), 40)).split(b'\0', 1)[0]
            token = self.index(arg(0), self.edict) + 1
            sample_id = int(sample == b'weapons/melee_hit.wav')
            assert sample in (b'weapons/melee_hit.wav', b'weapons/melee_swing.wav')
            self.event(1, token, sample_id, arg(1), arg(3), arg(4), arg(5), arg(6))
            if sample_id == 0:
                self.mutate(1)
            self.ret(0)
        elif address == EDGE + 0x4C:
            self.event(2, *self.vector(arg(0)))
            self.mutate(2)
            self.ret(0)
        elif address == EDGE + 0x6228:
            assert uc.reg_read(UC_X86_REG_ECX) == owner
            self.event(3)
            for i, v in enumerate((2, 3, 4)):
                self.write(arg(0) + i * 4, bits(v))
            self.mutate(3)
            self.ret(4, arg(0))
        elif address == 0x100F91B0:
            assert uc.reg_read(UC_X86_REG_ECX) == owner
            self.event(4, arg(0), arg(1), arg(2))
            self.ret(12)
        elif address == EDGE + 0x40:
            previous = self.index(arg(0), self.edict) + 1 if arg(0) else 0
            self.event(5, previous, *self.vector(arg(1)), arg(2))
            self.ret(0, self.edict(previous) if previous < self.count else 0)
        elif address == EDGE + 0x118:
            self.ret(0, 1)
        elif address == 0x10013EF0:
            self.event(6, self.index(uc.reg_read(UC_X86_REG_ECX), self.obj) + 1)
        elif address == 0x10013550:
            self.event(7, self.index(uc.reg_read(UC_X86_REG_ECX), self.obj) + 1)
        elif EDGE + 0x2000 <= address < EDGE + 0x3000:
            entity, offset = divmod(address - EDGE - 0x2000, 0x200)
            assert uc.reg_read(UC_X86_REG_ECX) == self.obj(entity)
            if offset == 0x78:
                self.event(6, entity + 1)
                self.ret(0, 0)
            elif offset == 0x64:
                assert arg(0) == self.pev(1) and arg(1) == self.pev(1)
                self.event(8, entity + 1, arg(2), arg(3))
                self.ret(16, self.damage_result)
            else:
                raise ValueError(f'Unexpected target edge {offset:x}')
        elif EDGE <= address < EDGE + 0x10000 and address != EDGE + 0xFF00:
            raise ValueError(f'Unexpected original edge {address:x}')

    def run(self, row):
        (stamina, infinite, self.count, mask, dead, self.mutation,
         self.new_stamina, self.new_infinite, self.damage_result) = row
        self.events = []
        self.write(self.obj(1) + 0x21F0, stamina)
        self.uc.mem_write(self.obj(1) + 0x21E8, bytes((infinite,)))
        for i in range(3):
            self.write(self.pev(1) + 0x74 + 4 * i, bits(4 + i))
        for i in range(6):
            self.write(self.pev(i) + 0x174, dead >> (i * 2) & 3)
            self.write(self.vtable(i) + 0x78,
                       0x10013EF0 if mask & (1 << i) else EDGE + 0x2000 + i * 0x200 + 0x78)
        self.write(STACK + 0xFF00, EDGE + 0xFF00)
        self.uc.reg_write(UC_X86_REG_ESP, STACK + 0xFF00)
        self.uc.reg_write(UC_X86_REG_ECX, self.obj(1))
        self.uc.emu_start(0x100E8B40, EDGE + 0xFF00, count=20000)
        assert self.uc.reg_read(UC_X86_REG_EIP) == EDGE + 0xFF00
        return [self.read(self.obj(1) + 0x21F0), *self.events]


def generate(sample, proof, cases, expected):
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    pre = json.loads(proof.read_text('utf8'))
    if not pre['beforeImplementation'] or digest(sample) != pre['sampleSha256'] or digest(sample) != shared.SHA:
        raise ValueError('Original identity or pre-edit proof differs')
    mapper_spec = importlib.util.spec_from_file_location('pushback_mapper', ROOT / 'tests/cryoffear-magazine-actions/run.py')
    mapper = importlib.util.module_from_spec(mapper_spec)
    mapper_spec.loader.exec_module(mapper)
    image = mapper.mapped_image(sample.read_bytes())
    for region in pre['regions']:
        data = bytes.fromhex(region['hex'])
        start = int(region['va'], 16) - BASE
        if image[start:start + len(data)] != data or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Original IDA window differs')
    for name, sha in pre['archives'].items():
        if digest(proof.parent / name) != sha:
            raise ValueError('Original archived evidence differs')
    machine = Pushback(sample)
    rng = random.Random(0xE8B40)
    for i in range(1024):
        row = [bits(rng.choice((-1, -0.0, 0, .001, 1, 14.999, 15, 15.001, 100, 65536.125))),
               rng.randrange(2), i % 7, rng.randrange(64), rng.randrange(4096), i % 4,
               bits(rng.choice((-17, .01, 15, 61.05, 100, 65536.125))), rng.randrange(2), rng.randrange(2)]
        cases.write(' '.join(f'{v:08x}' for v in row) + '\n')
        expected.write(' '.join(f'{v:08x}' for v in machine.run(row)) + '\n')
    return dict(sampleSha256=shared.SHA, proofSha256=digest(proof), cases=1024,
                originalFunction='100E8B40', originalBytes=337,
                actualHelpers=['101186B0', '10119320', '10013EF0', '10013550'],
                visitedInstructions=len(machine.visited),
                boundary='Complete original player pushback; controlled engine sphere, vectors, position, sound/recoil and target damage edges; no SDK damage translation, owner scheduling, deployment or gameplay')
