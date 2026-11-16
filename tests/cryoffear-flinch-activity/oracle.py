# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the complete original flinch selector with controlled model lookups."""
import hashlib
import importlib.util
import json
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('flinch_original', ROOT / 'tests/cryoffear-ballistics/oracle.py')
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)


class Flinch(shared.Original):
    def hook(self, uc, address, size, user):
        if shared.BASE <= address < shared.BASE + 0x300000:
            self.visited.add(address)
        sp = uc.reg_read(UC_X86_REG_ESP)
        if address == 0x10119320:
            assert self.read(sp + 4) == self.pev(1) + 0x50
            self.events.append(1)
            self.write(self.obj(1) + 0x21c, self.group_after_vectors)
            self.ret(0)
        elif address == 0x1000e950:
            assert uc.reg_read(UC_X86_REG_ECX) == self.obj(1)
            self.events.extend((2, self.read(sp + 4)))
            result = self.first if self.lookups == 0 else self.fallback
            self.lookups += 1
            assert self.lookups <= 2
            self.ret(4, result)
        elif shared.EDGE <= address < shared.EDGE + 0x10000 and address != shared.EDGE + 0xff00:
            raise ValueError(f'Unexpected original callback {address:08x}')

    def run(self, row):
        initial, self.group_after_vectors, self.first, self.fallback = row
        self.write(self.obj(1) + 0x21c, initial)
        self.events = []
        self.lookups = 0
        self.write(shared.STACK + 0xff00, shared.EDGE + 0xff00)
        self.uc.reg_write(UC_X86_REG_ESP, shared.STACK + 0xff00)
        self.uc.reg_write(UC_X86_REG_ECX, self.obj(1))
        self.uc.emu_start(0x10089c80, shared.EDGE + 0xff00, count=2000)
        assert self.uc.reg_read(UC_X86_REG_EIP) == shared.EDGE + 0xff00
        return [self.uc.reg_read(shared.UC_X86_REG_EAX), len(self.events), *self.events]


def generate(sample, proof, cases, expected):
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    pre = json.loads(proof.read_text('utf8'))
    if not pre['beforeImplementation'] or digest(sample) != pre['sampleSha256'] or digest(sample) != shared.SHA:
        raise ValueError('Original identity or pre-edit proof differs')
    spec = importlib.util.spec_from_file_location('flinch_mapper', ROOT / 'tests/cryoffear-magazine-actions/run.py')
    mapper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mapper)
    image = mapper.mapped_image(sample.read_bytes())
    for region in pre['regions']:
        data = bytes.fromhex(region['hex'])
        start = int(region['va'], 16) - shared.BASE
        if image[start:start + len(data)] != data or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Original IDA bytes differ')
    for name, sha in pre['archives'].items():
        if digest(proof.parent / name) != sha:
            raise ValueError('Archived evidence differs')
    machine = Flinch(sample)
    count = 0
    groups = [*range(-2, 33), -2147483648, 2147483647]
    for group in groups:
        for first in (-2, -1, 0, 3):
            for fallback in (-2, -1, 0, 7):
                row = [(group + 7) & 0xffffffff, group & 0xffffffff, first & 0xffffffff, fallback & 0xffffffff]
                cases.write(' '.join(f'{v:08x}' for v in row) + '\n')
                expected.write(' '.join(f'{v:08x}' for v in machine.run(row)) + '\n')
                count += 1
    return dict(sampleSha256=shared.SHA, proofSha256=digest(proof), cases=count,
                function='10089C80..10089D09', codeBytes=137, bytesWithTables=191,
                visitedInstructions=len(machine.visited),
                boundary='Full original helper; controlled MakeVectors and model LookupActivity, no task/schedule/target gameplay')
