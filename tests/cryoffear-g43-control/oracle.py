# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute complete original G43 reload/pushback starts with real model timings."""
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import struct

from unicorn.x86_const import UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parents[2]
SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
MODEL_SHA = 'bb14a6182a47e36fe89973aa6a78a6a65caac6529141b8e2287605b32f249031'
spec = importlib.util.spec_from_file_location('g43_magazine_abi', ROOT / 'tests/cryoffear-magazine-actions/run.py')
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
shared.FUNCS.update({(0, 7): 0x10044E60, (1, 7): 0x10044D60})
shared.ORIGINAL_ID[7] = 26
bits = shared.bits


def number(value):
    return struct.unpack('<f', struct.pack('<I', value))[0]


class Original(shared.Original):
    def __init__(self, image, model):
        super().__init__(image, {7: model})

    def snapshot(self):
        return super().snapshot() + [self.read(shared.GLOBAL)]

    def mutate(self, clip=False):
        if self.changed:
            self.write(shared.GLOBAL, bits(number(self.read(shared.GLOBAL)) + self.delta))
            if clip:
                self.write(shared.WEAPON + 0x1E4, not self.read(shared.WEAPON + 0x1E4))

    def hook(self, uc, address, size, user):
        self.visited.add(address)
        sp = uc.reg_read(UC_X86_REG_ESP)
        if address == shared.STUB:
            sequence = self.read(sp + 4)
            self.event(1, sequence)
            self.write(shared.OPEV + 0x21C, sequence)
            self.mutate()
            self.ret(12)
        elif address == shared.STUB + 0x10:
            self.event(0, 26, self.read(sp + 4))
            self.mutate(clip=True)
            self.ret(4)
        elif address == 0x100EA240:
            name = bytes(uc.mem_read(self.read(sp + 4), 64)).split(b'\0', 1)[0]
            if name == b'm16_melee': kind = 2
            elif name == b'm16_reload': kind = 3
            elif name == b'm16_reloadempty': kind = 4
            else: raise ValueError('Unexpected G43 player action: ' + repr(name))
            self.event(kind, 26)
            self.mutate()
            self.ret(8)
        else:
            super().hook(uc, address, size, user)

    def run(self, row):
        self.changed, self.delta = row[17], number(row[18])
        self.write(shared.OWNER + 0x21F0, row[16])
        self.write(shared.OWNER + 0x78, 0)
        return super().run([row[0], 7, *row[1:15], row[15]])


def generate(sample, model, proof, cases, expected, timing):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(sample) != SHA or sha(model) != MODEL_SHA:
        raise ValueError('Original G43 module/model identity mismatch')
    evidence = json.loads(proof.read_text('utf-8-sig'))
    if not evidence['beforeImplementation'] or evidence['sampleSha256'] != SHA or evidence['modelSha256'] != MODEL_SHA:
        raise ValueError('Missing matching pre-implementation IDA evidence')
    for name, digest in evidence['archives'].items():
        if sha(proof.parent / name) != digest:
            raise ValueError('Changed IDA evidence: ' + name)
    image = shared.mapped_image(sample.read_bytes())
    required = {0x10044E60, 0x10044D60, 0x1011C560, 0x10120210, 0x1000F390, 0x1001F050, 0x1011AB20}
    covered = []
    for region in evidence['regions']:
        address = int(region['va'], 16)
        data = bytes.fromhex(region['hex'])
        if not data or hashlib.sha256(data).hexdigest() != region['sha256'] or image[address-shared.BASE:address-shared.BASE+len(data)] != data:
            raise ValueError('Original/IDA bytes mismatch: ' + region['va'])
        covered.append((address, address + len(data)))
        required.discard(address)
    if required:
        raise ValueError('Incomplete original call coverage')
    machine = Original(image, model.read_bytes())
    timing.write(' '.join(f'{v:08x}' for v in shared.model_times(model.read_bytes()) + [0] * 5) + '\n')
    rng = random.Random(4326)
    count = 0
    for operation in range(2):
        for i in range(2048):
            now = bits(rng.choice((.10506151616573334, 0, 10.25, 8192.3, 1000000, 16777216)))
            clocks = [now] * 5
            if i % 4 < 3:
                clocks[i % 5] = bits(number(now) + (-1, 0, 1)[i % 4])
            clip = rng.choice((0, 1, 7, 9, 10) if operation else (0, 1, 10, 75, 511, 10000))
            row = [operation, clip, rng.choice((0, 1, 11, 200)), rng.choice((0, 0, 0, 1, 2, 5)),
                rng.choice((0, 0, 0, 1)), 3, 2, rng.randrange(2), int(operation and i % 13 == 0),
                0, *clocks, now, bits(rng.choice((-1, 0, .1, 14, 15, 100))), i % 3 != 0, bits(.125)]
            cases.write(' '.join(f'{int(v):08x}' for v in row) + '\n')
            expected.write(' '.join(f'{v:08x}' for v in machine.run(row)) + '\n')
            count += 1
    # The hook observes this ABI entry and returns before its instruction runs.
    # Its player-animation implementation is deliberately outside this replay.
    controlled = {0x100EA240}
    executed = {address for address in machine.visited
        if shared.BASE <= address < shared.BASE + len(image) and address not in controlled}
    uncovered = sorted(address for address in executed
        if not any(start <= address < end for start, end in covered))
    if uncovered:
        raise ValueError('Executed original instructions outside the pre-edit IDA proof: ' +
            ','.join(f'{address:08X}' for address in uncovered))
    return dict(sampleSha256=SHA, modelSha256=MODEL_SHA, proofSha256=sha(proof), cases=count,
        visitedInstructions=len(machine.visited), originalInstructions=len(executed),
        preeditCoverageComplete=True, controlledOriginalEntries=['100EA240'],
        functions=['10044E60', '10044D60'],
        actualHelpers=['1011C560', '10120210', '1000F390', '1001F050', '1011AB20'],
        boundary='Complete ordinary G43 reload/pushback starts and original model duration; controlled player scheduling/animation callbacks. No primary/aim, SDK admission, deployment, gameplay or live performance.')
