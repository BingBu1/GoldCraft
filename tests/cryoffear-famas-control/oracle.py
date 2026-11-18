# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute complete matched FAMAS controls and original model-duration helpers."""
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import random
import struct

from unicorn.x86_const import UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parents[2]
SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
MODEL_SHA = '175cb022365adf25dc333b14b63dc7c1d6119817ea65f43f817acc6a7c631e54'
spec = importlib.util.spec_from_file_location('famas_magazine_abi', ROOT / 'tests/cryoffear-magazine-actions/run.py')
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
shared.FUNCS.update({(0, 8): 0x1003C370, (1, 8): 0x1003C2C0})
shared.ORIGINAL_ID[8] = 23
bits = shared.bits


def number(value):
    return struct.unpack('<f', struct.pack('<I', value))[0]


class Original(shared.Original):
    def __init__(self, image, model):
        super().__init__(image, {8: model})
        # Actual GetItemInfo registers this capacity at runtime; the PE's BSS is zero.
        self.write(0x102318A4 + 23 * 0x2C, 30)

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
            self.event(0, 23, self.read(sp + 4))
            self.mutate(clip=True)
            self.ret(4)
        elif address == 0x100EA240:
            raise ValueError('FAMAS must not invent a named player animation')
        else:
            super().hook(uc, address, size, user)

    def run(self, row):
        self.changed, self.delta = row[17], number(row[18])
        self.write(shared.OWNER + 0x21F0, row[16])
        self.write(shared.OWNER + 0x78, 0)
        return super().run([row[0], 8, *row[1:15], row[15]])


def generate(sample, model, proof, cases, expected, timing):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(sample) != SHA or sha(model) != MODEL_SHA:
        raise ValueError('Original FAMAS module/model identity mismatch')
    evidence = json.loads(proof.read_text('utf-8-sig'))
    if not evidence['beforeImplementation'] or evidence['sampleSha256'] != SHA or evidence['modelSha256'] != MODEL_SHA:
        raise ValueError('Missing matching pre-implementation IDA evidence')
    for name, digest in evidence['archives'].items():
        if sha(proof.parent / name) != digest:
            raise ValueError('Changed IDA evidence: ' + name)
    image = shared.mapped_image(sample.read_bytes())
    required = {0x1003C370, 0x1003C2C0, 0x1003ADA0, 0x101AE8DC,
                0x1011C560, 0x10120210, 0x1000F390, 0x1001F050, 0x1011AB20}
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
    data = model.read_bytes()
    sequence_count = shared.u32(data, 164)
    durations = shared.model_times(data)[:sequence_count]
    if len(durations) > 43:
        raise ValueError('FAMAS model does not fit the prepared timing storage')
    timing.write(' '.join(f'{v:08x}' for v in durations + [0] * (43-len(durations))) + '\n')
    machine = Original(image, data)
    rng = random.Random(2323)
    count = zero_reserve = applied_reload = 0
    for operation in range(2):
        clips = (0, 1, 29, 30) if operation else (0, 1, 30, 75, 511, 10000)
        for clip, reserve, action, special, aimed, pending, locked in itertools.product(
                clips, (0, 1, 150), (0, 1, 2, 5), (0, 1), (0, 1), (0, 1), (0, 1, 2)):
            now = bits(10.25)
            clocks = [now] * 5
            if locked:
                clocks[locked-1] = bits(11.25)
            row = [operation, clip, reserve, action, special, 3, 2, aimed, pending,
                   0, *clocks, now, bits(100), 0, bits(.125)]
            result = machine.run(row)
            cases.write(' '.join(f'{int(v):08x}' for v in row) + '\n')
            expected.write(' '.join(f'{v:08x}' for v in result) + '\n')
            count += 1
            if operation and result[8]:
                applied_reload += 1
                zero_reserve += reserve == 0
        for i in range(1024):
            now = bits(rng.choice((.10506151616573334, 0, 10.25, 8192.3, 1000000, 16777216)))
            clocks = [now] * 5
            clocks[i % 5] = bits(number(now) + (-1, 0, 1)[i % 3])
            row = [operation, rng.choice(clips), rng.choice((0, 1, 150)),
                   rng.choice((0, 0, 0, 1, 2, 5)), rng.choice((0, 0, 1)), 3, 2,
                   rng.randrange(2), int(operation and i % 17 == 0), 0, *clocks, now,
                   bits(rng.choice((-1, 0, .1, 14, 15, 100))), 1, bits(.125)]
            cases.write(' '.join(f'{int(v):08x}' for v in row) + '\n')
            expected.write(' '.join(f'{v:08x}' for v in machine.run(row)) + '\n')
            count += 1
    if not zero_reserve:
        raise ValueError('Missing actual ID23 zero-reserve reload coverage')
    executed = {a for a in machine.visited if shared.BASE <= a < shared.BASE + len(image)}
    uncovered = sorted(a for a in executed if not any(start <= a < end for start, end in covered))
    if uncovered:
        raise ValueError('Original instructions outside pre-edit IDA proof: ' + ','.join(f'{a:08X}' for a in uncovered))
    return dict(sampleSha256=SHA, modelSha256=MODEL_SHA, proofSha256=sha(proof), cases=count,
        originalInstructions=len(executed), preeditCoverageComplete=True,
        actualSequenceCount=sequence_count, ordinaryReloads=applied_reload, zeroReserveReloads=zero_reserve,
        functions=['1003C370', '1003C2C0'],
        actualHelpers=['1011C560', '10120210', '1000F390', '1001F050', '1011AB20'],
        boundary='Complete ordinary FAMAS reload/pushback starts with real x86/model timings; controlled player scheduling/view delivery. No reload completion, full gun, SDK, deployment, gameplay or performance acceptance.')
