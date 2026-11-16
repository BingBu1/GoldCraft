# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay original 870 controls; PostFrame probes are explicit subgraphs only."""
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import random
import struct

from unicorn.x86_const import (
    UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP,
    UC_X86_REG_FPCW, UC_X86_REG_MXCSR,
)

ROOT = Path(__file__).resolve().parents[2]
SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
spec = importlib.util.spec_from_file_location('shotgun_original_abi', ROOT / 'tests/cryoffear-magazine-actions/run.py')
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
bits = shared.bits


def number(value):
    return struct.unpack('<f', struct.pack('<I', value))[0]


def f32(value):
    return number(bits(value))


class Original(shared.Original):
    def __init__(self, image):
        super().__init__(image, {})
        self.write(shared.VTABLE + 8, shared.STUB + 0x30)
        self.write(shared.VTABLE + 0x1C0, 0x1006E3E0)
        self.operations = [0] * 5

    def snapshot(self):
        return [self.read(shared.WEAPON + 0x1E4), self.read(shared.OWNER + 0x2544)] + \
            [self.read(shared.WEAPON + offset) for offset in (0x128, 0x140, 0x1C8)] + \
            [self.read(shared.WPEV + offset) for offset in (0x248, 0x24C)] + \
            [self.byte(shared.WEAPON + offset) for offset in (0x178, 0x1A4)] + \
            [self.read(shared.WEAPON + offset) for offset in (0x1CC, 0x1D0, 0x1D4, 0x1D8)] + \
            [self.read(shared.OWNER + 0x308), self.read(shared.OWNER + 0x4F4),
             self.read(shared.WEAPON + 0x1FC), self.read(shared.GLOBAL)]

    def mutate(self, view=False):
        if self.mutation:
            self.write(shared.GLOBAL, bits(f32(number(self.read(shared.GLOBAL)) + self.delta)))
        if self.mutation == 2 and view:
            self.write(shared.WEAPON + 0x1C8, 9)
            self.write(shared.WPEV + 0x248, not self.read(shared.WPEV + 0x248))
            self.write(shared.WPEV + 0x24C, not self.read(shared.WPEV + 0x24C))

    def hook(self, uc, address, size, user):
        # These boundaries are observed before their instruction executes.
        if (self.operation == 3 and address == 0x1006D857) or \
                (self.operation == 4 and address == 0x1006D424):
            uc.reg_write(UC_X86_REG_EIP, shared.STOP)
            return
        self.visited.add(address)
        sp = uc.reg_read(UC_X86_REG_ESP)
        if address == shared.STUB:
            self.event(0, self.read(sp + 4))
            self.mutate(view=True)
            self.ret(12)
        elif address in (shared.STUB + 0x10, shared.STUB + 0x30):
            self.event(4 if address == shared.STUB + 0x10 else 3, 0, self.read(sp + 4))
            self.mutate()
            self.ret(4)
        elif address == 0x100EA240:
            text = bytes(uc.mem_read(self.read(sp + 4), 64)).split(b'\0', 1)[0]
            names = {b'shotgun_reload_start': 1, b'shotgun_reload_insert': 2,
                     b'shotgun_melee': 5, b'shotgun_reload_endempty': 6,
                     b'shotgun_reloadend': 7, b'shotgun_reload_end': 8}
            if text not in names:
                raise ValueError('Unexpected original shotgun action: ' + repr(text))
            self.event(names[text])
            self.mutate()
            self.ret(8)
        elif not shared.BASE <= address < shared.BASE + 0x250000:
            raise ValueError(f'Unexpected external instruction {address:08X}')

    def run(self, row):
        self.operation, self.mutation, self.delta = row[0], row[20], number(row[19])
        self.operations[self.operation] += 1
        self.events = []
        state = row[1:17]
        for offset, value in zip((0x1E4, 0x128, 0x140, 0x1C8), (state[0], *state[2:5])):
            self.write(shared.WEAPON + offset, value)
        self.write(shared.OWNER + 0x2544, state[1])
        for offset, value in zip((0x248, 0x24C), state[5:7]):
            self.write(shared.WPEV + offset, value)
        for offset, value in zip((0x178, 0x1A4), state[7:9]):
            self.byte(shared.WEAPON + offset, value)
        for offset, value in zip((0x1CC, 0x1D0, 0x1D4, 0x1D8), state[9:13]):
            self.write(shared.WEAPON + offset, value)
        self.write(shared.OWNER + 0x308, state[13])
        self.write(shared.OWNER + 0x4F4, state[14])
        self.write(shared.WEAPON + 0x1FC, state[15])
        self.write(shared.GLOBAL, row[17])
        self.write(shared.OWNER + 0x21F0, row[18])
        self.write(shared.OWNER + 0x22B0, int(bool(row[21])))
        self.write(shared.STACK, shared.STOP)
        self.uc.reg_write(UC_X86_REG_ECX, shared.WEAPON)
        self.uc.reg_write(UC_X86_REG_ESI, shared.WEAPON)
        self.uc.reg_write(UC_X86_REG_ESP, shared.STACK)
        self.uc.reg_write(UC_X86_REG_FPCW, 0x27F)
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        entry = (0x1006E3E0, 0x1006E6F0, 0x1006E620, 0x1006D6BF, 0x1006D3ED)[self.operation]
        self.uc.emu_start(entry, shared.STOP, count=2000)
        if self.uc.reg_read(UC_X86_REG_EIP) != shared.STOP:
            raise ValueError('Original shotgun control did not reach its return/boundary')
        return [*self.snapshot(), len(self.events), *itertools.chain.from_iterable(self.events)]


def generate(sample, proof, cases, expected):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(sample) != SHA:
        raise ValueError('Original shotgun module identity mismatch')
    evidence = json.loads(proof.read_text('utf-8-sig'))
    if not evidence['beforeImplementation'] or evidence['sampleSha256'] != SHA:
        raise ValueError('Missing matching pre-edit shotgun evidence')
    for name, digest in evidence['archives'].items():
        if sha(proof.parent / name) != digest:
            raise ValueError('Changed original IDA archive: ' + name)
    image = shared.mapped_image(sample.read_bytes())
    covered = []
    for region in evidence['regions'] + evidence['additionalRegions']:
        address, size = int(region['va'], 16), region['size']
        data = image[address - shared.BASE:address - shared.BASE + size]
        if len(data) != size or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Original/IDA region mismatch: ' + region['va'])
        covered.append((address, address + size))
    machine = Original(image)
    rng = random.Random(870)
    count = 0

    def emit(row):
        nonlocal count
        answer = machine.run(row)
        cases.write(' '.join(f'{int(v) & 0xffffffff:08x}' for v in row) + '\n')
        expected.write(' '.join(f'{v:08x}' for v in answer) + '\n')
        count += 1

    def initial(operation=0, now=13.37):
        return [operation, 1, 50, 0, 0, 0, 0, 0, 0, 0,
                bits(0), bits(0), bits(0), bits(0), bits(-2), bits(-3), bits(-4),
                bits(now), bits(100), bits(.125), 0, 0]

    # Guaranteed reload and all three finish labels/orderings, including callback
    # state/clock mutation. Prefix checks separately preserve the pressed edge.
    for stage, clip, reserve, aiming, pending, mutation in itertools.product(
            (0, 1, 2, -1), (0, 1, 4, 5, 75), (0, 1, 50), (0, 1), (0, 1), range(3)):
        row = initial(); row[1:3] = [clip, reserve]
        row[5], row[8], row[9], row[20] = stage, aiming, pending, mutation
        emit(row)
    for stage, abort, empty, clip, reserve, mutation in itertools.product(
            (0, 1, 2), (0, 1), (0, 1), (0, 1, 5), (0, 1), range(3)):
        row = initial(3); row[1:3] = [clip, reserve]
        row[5:8], row[20] = [stage, empty, abort], mutation
        emit(row)
    for operation in range(5):
        for i in range(1024 if operation != 1 else 64):
            now = rng.choice((.10506151616573334, 0, 10.25, 8192.3, 1000000, 16777216))
            row = initial(operation, now)
            row[1:3] = [rng.choice((-1, 0, 1, 4, 5, 75, 511, 10000, 0x7fffffff)),
                        rng.choice((-0x80000000, -1, 0, 1, 50, 65535))]
            row[3:10] = [rng.choice((-1, 0, 0, 0, 1, 2, 3)), rng.choice((0, 0, 0, 1)),
                         rng.choice((-1, 0, 1, 1, 2, 7)), rng.randrange(2), rng.randrange(2),
                         int(i % 5 == 0), int(i % 11 == 0)]
            for at in (10, 11, 13):
                row[at] = bits(f32(now + rng.choice((-1, 0, 0, 1))))
            row[18], row[20], row[21] = bits(rng.choice((-1, 0, .0001, 15, 100))), i % 3, i % 2
            emit(row)
    # Exact timer boundary and adjacent representable values, including .5/.51
    # double additions versus the final +1 float operation at large map times.
    for now in (.10506151616573334, 13.37, 8192.3, 1000000, 16777216):
        for operation, stage, clock, mutation in itertools.product((0, 2, 3), (0, 1), (-1, 0, 1), range(3)):
            row = initial(operation, now); row[5], row[20] = stage, mutation
            edge = bits(now) + clock
            row[10] = row[11] = row[13] = max(0, edge)
            emit(row)
    controlled = {0x100EA240}
    executed = {address for address in machine.visited
                if shared.BASE <= address < shared.BASE + len(image) and address not in controlled}
    uncovered = sorted(address for address in executed if not any(lo <= address < hi for lo, hi in covered))
    if uncovered:
        raise ValueError('Original instruction outside pre-edit evidence: ' + ','.join(f'{v:08X}' for v in uncovered))
    required = {0x1006E414, 0x1006E472, 0x1006E4A9, 0x1006E583, 0x1006E609,
                0x1006E69E, 0x1006D41A, 0x1006D767, 0x1006D7D7, 0x1006D739, 0x1006D7B4}
    if required - executed:
        raise ValueError('Original branch was not exercised: ' + repr(sorted(required - executed)))
    return dict(sampleSha256=SHA, proofSha256=sha(proof), cases=count,
                operations=machine.operations, originalInstructions=len(executed),
                preeditCoverageComplete=True, controlledOriginalEntries=['100EA240'],
                wholeFunctions=['1006E3E0', '1006E6F0', '1006E620'],
                subgraphs=[['1006D6BF', '1006D857'], ['1006D3ED', '1006D424']],
                boundary='Complete reload, shell-insert and pushback starts; two explicit PostFrame subgraphs only. '
                         'Controlled animation/scheduling edges; no complete frame, SDK weapon, shop, deployment, gameplay or live performance.')
