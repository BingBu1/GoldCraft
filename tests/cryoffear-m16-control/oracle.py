# SPDX-License-Identifier: GPL-3.0-or-later
"""Original M16 wrappers, with Shoot controlled at its ABI boundary."""
import hashlib
import importlib.util
import json
from pathlib import Path
import random

from unicorn.x86_const import UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parents[2]
SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


shared = load('m16_magazine_abi', ROOT / 'tests/cryoffear-magazine-actions/run.py')
bits = shared.bits
W, O, G, V, STUB = shared.WEAPON, shared.OWNER, shared.GLOBAL, shared.VTABLE, shared.STUB
shared.FUNCS.update({(0, 6): 0x10051D50, (1, 6): 0x10052D20, (2, 6): 0x10051ED0})
shared.ORIGINAL_ID[6] = 9


def number(value):
    import struct
    return struct.unpack('<f', struct.pack('<I', value))[0]


class Original(shared.Original):
    def __init__(self, image, model):
        super().__init__(image, {6: model})
        self.write(V + 400, STUB + 0x40)
        self.write(V + 448, STUB + 0x50)

    def snapshot(self):
        return super().snapshot() + [self.byte(W + 512), self.read(G)]

    def mutate_clock(self):
        if self.mutate:
            self.write(G, bits(number(self.read(G)) + self.delta))

    def hook(self, uc, address, size, user):
        self.visited.add(address)
        sp = uc.reg_read(UC_X86_REG_ESP)
        if address == STUB:
            sequence = self.read(sp + 4)
            self.event(3, sequence)
            self.write(shared.OPEV + 0x21C, sequence)
            if self.operation == 1:
                self.mutate_clock()
            self.ret(12)
        elif address == STUB + 0x40:
            self.event(1)
            self.mutate_clock()
            self.ret()
        elif address == STUB + 0x50:
            self.event(2)
            self.ret()
        elif address == 0x10052120:
            self.event(0)
            self.write(W + 0x1E4, self.read(W + 0x1E4) - 1)
            self.write(W + 0x1F8, self.read(W + 0x1F8) + 1)
            if self.mutate:
                self.byte(W + 512, not self.byte(W + 512))
                self.mutate_clock()
            self.ret()
        elif address == 0x100EA240:
            name = bytes(uc.mem_read(self.read(sp + 4), 64)).split(b'\0', 1)[0]
            assert name in (b'm16_reload', b'm16_reloadempty'), name
            self.event(5 if name.endswith(b'empty') else 4, 9)
            self.ret(8)
        else:
            super().hook(uc, address, size, user)

    def run(self, row):
        self.operation, self.mutate, self.delta = row[0], row[19], number(row[20])
        self.byte(W + 512, row[15])
        self.write(W + 0x78, 0)
        self.write(O + 0x22B0, row[17])
        self.write(W + 0x1B8, row[18])
        return super().run([row[0], 6, *row[1:15], row[16]])


def generate(sample, model, proof, cases, expected, timing):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(sample) != SHA or sha(model) != '8aaa8cc0ce4badc51b2f176752f8a858cddf95b113e011eff97ad912b7aa467b':
        raise ValueError('Original module/model identity mismatch')
    archived = json.loads(proof.read_text('utf-8-sig'))
    if not archived['beforeImplementation'] or archived['sampleSha256'] != SHA:
        raise ValueError('Missing pre-implementation IDA evidence')
    for name, digest in archived['archives'].items():
        if sha(proof.parent / name) != digest:
            raise ValueError('Changed IDA archive ' + name)
    image = shared.mapped_image(sample.read_bytes())
    for region in archived['regions']:
        data = bytes.fromhex(region['hex'])
        at = int(region['va'], 16) - shared.BASE
        if image[at:at + len(data)] != data or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Original IDA instruction mismatch')
    machine = Original(image, model.read_bytes())
    times = shared.model_times(model.read_bytes()) + [0] * 5
    timing.write(' '.join(f'{v:08x}' for v in times) + '\n')
    rng = random.Random(516)
    count = 0
    for op in range(3):
        for i in range(1536):
            now = bits(rng.choice((.10506151616573334, 1.1, 13.37, 4096.125, 8192.3, 65536.125)))
            clocks = [0] * 5
            if i % 6 < 3:
                clocks[i % 2] = (0, now, bits(number(now) + 1))[i % 3]
            clip = rng.choice((0, 1, 7, 19, 20) if op == 2 else (0, 1, 20, 75, 511, 10000))
            # op, fourteen original state words, burst mode, time, pressed,
            # fire-on-empty, controlled callback mutation, clock delta.
            row = [op, clip, rng.choice((0, 1, 21, 200)), rng.choice((0, 0, 0, 1, 2, 5)),
                rng.choice((0, 0, 0, 1)), 3, rng.choice((0, 2, 3, 8)), rng.randrange(2),
                int(op == 2 and i % 13 == 0), 0, *clocks, rng.randrange(2), now,
                int(i % 7 != 0), rng.randrange(2), int(i % 3 != 0), bits(.125)]
            cases.write(' '.join(f'{v:08x}' for v in row) + '\n')
            expected.write(' '.join(f'{v:08x}' for v in machine.run(row)) + '\n')
            count += 1
    return dict(sampleSha256=SHA, modelSha256=sha(model), proofSha256=sha(proof), cases=count,
        visitedInstructions=len(machine.visited), wrapperFunctions=['10051D50', '10052D20', '10051ED0'],
        actualReloadHelpers=['1011C560', '10120210', '1000F390', '1001F050', '1011AB20'],
        boundary='Complete M16 wrappers and original reload duration; controlled Shoot, view/player animation and clock mutation. Not full Shoot, burst continuation, SDK, gameplay or performance acceptance.')
