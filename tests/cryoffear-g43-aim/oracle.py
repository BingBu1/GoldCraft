# SPDX-License-Identifier: GPL-3.0-or-later
"""Original G43 aim virtual methods, toggle, messages and actual model duration."""
import hashlib
import importlib.util
import json
from pathlib import Path
import random

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_MXCSR

ROOT = Path(__file__).resolve().parents[2]
SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
MODEL_SHA = 'bb14a6182a47e36fe89973aa6a78a6a65caac6529141b8e2287605b32f249031'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


shared = load('g43_aim_transitions', ROOT / 'tests/cryoffear-tmp-weapon/transition_oracle.py')
mapper = load('g43_aim_model', ROOT / 'tests/cryoffear-magazine-actions/run.py')
bits = shared.bits
number = shared.shared.number
WORK, STACK, EDGE = shared.WORK, shared.STACK, shared.EDGE
OFFSETS = (0x1E4, 0x178, 0x1CC, 0x1D0, 0x1D4, 0x1D8)


class Aim(shared.Transitions):
    sequence_count = 38
    enter, exit = 0x10043AD0, 0x100437F0

    def __init__(self, sample, model):
        super().__init__(sample)
        # The duration helper reads only the real model's count and descriptors.
        start = mapper.u32(model, 168)
        self.uc.mem_write(self.model, model[:244])
        self.write(self.model + 168, 244)
        self.uc.mem_write(self.model + 244, model[start:start + 38 * 176])
        self.write(0x10226668, 93); self.write(0x102265CC, 94)
        self.visited = set()

    def snapshot(self):
        return [self.read(self.weapon + at) for at in OFFSETS] + [self.read(self.pev(1) + 0x274),
            self.read(self.pev(1) + 0x21C), self.read(self.globals)]

    def observe(self, kind, *values):
        self.events += [kind, len(values) + 9, *values, *self.snapshot()]
        self.ordinal += 1
        if self.ordinal != self.nth: return
        if self.change & 1: self.write(self.globals, bits(number(self.read(self.globals)) + .125))
        if self.change & 2: self.write(self.weapon + 0x1E4, not self.read(self.weapon + 0x1E4))
        if self.change & 4: self.write(self.pev(1) + 0x21C, self.alt)

    def hook(self, uc, address, size, user):
        self.visited.add(address)
        sp = uc.reg_read(UC_X86_REG_ESP); arg = lambda n: self.read(sp + 4 + n * 4)
        if address == EDGE + 0xB8:
            self.message, self.fields = arg(1), []; self.ret(0)
        elif address in (EDGE + 0xC0, EDGE + 0xD4):
            self.fields.append(arg(0)); self.ret(0)
        elif address == EDGE + 0xBC:
            assert self.message in (93, 94)
            assert len(self.fields) == (5 if self.message == 93 else 3)
            self.observe(0 if self.message == 93 else 1, *self.fields); self.ret(0)
        elif address in (0x10043D3E, 0x10043A3F):
            self.observe(2, self.read(self.pev(1) + 0x274))
        elif address == EDGE + 0x7198:
            self.observe(3, arg(0)); self.write(self.pev(1) + 0x21C, arg(0)); self.ret(12)
        elif address == 0x1000F390:
            self.observe(4, arg(1))  # The helper executes after the observation.
        else:
            super().hook(uc, address, size, user)

    def run(self, row):
        (op, now, clip, aim, primary, secondary, extra, action, special, pressed,
         toggle, x, y, z, zoom, self.change, self.nth, self.alt) = row
        self.events, self.ordinal, self.last = [], 0, []
        for at, value in zip(OFFSETS, (clip, aim, primary, secondary, extra, 0)):
            self.write(self.weapon + at, value)
        for at, value in ((0x128, action), (0x140, special), (0x17C, x), (0x180, y), (0x184, z), (0x188, zoom)):
            self.write(self.weapon + at, value)
        self.write(self.globals, now); self.write(self.obj(1) + 0x22B0, pressed)
        self.write(WORK + 0xCE000, bits(toggle))
        self.write(self.pev(1) + 0x274, bits(90)); self.write(self.pev(1) + 0x21C, 0)
        self.uc.reg_write(UC_X86_REG_ESP, STACK + 0xFF00)
        self.write(STACK + 0xFF00, EDGE + 0xFF00)
        self.uc.reg_write(UC_X86_REG_ECX, self.weapon)
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        self.uc.emu_start((self.enter, self.exit, 0x100453B0)[op], EDGE + 0xFF00, count=15000)
        if self.uc.reg_read(UC_X86_REG_EIP) != EDGE + 0xFF00: raise ValueError('Original aim failed to return')
        return self.snapshot() + self.events


def generate(sample, model, proof, cases, expected, timing):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(sample) != SHA or sha(model) != MODEL_SHA: raise ValueError('Original aim identities mismatch')
    pre = json.loads(proof.read_text('utf-8-sig'))
    if not pre['beforeImplementation'] or pre['sampleSha256'] != SHA or pre['modelSha256'] != MODEL_SHA:
        raise ValueError('Missing original pre-edit aim evidence')
    image = mapper.mapped_image(sample.read_bytes()); covered = []
    for region in pre['regions']:
        start = int(region['va'], 16); data = bytes.fromhex(region['hex'])
        if not data or image[start-mapper.BASE:start-mapper.BASE+len(data)] != data or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Original aim bytes mismatch')
        covered.append((start, start + len(data)))
    for name, digest in pre['archives'].items():
        if sha(proof.parent / name) != digest: raise ValueError('Changed IDA evidence: ' + name)
    raw_model = model.read_bytes(); original = Aim(sample, raw_model)
    timing.write(' '.join(f'{v:08x}' for v in mapper.model_times(raw_model) + [0] * 5) + '\n')
    rng = random.Random(432675); count = 1536
    for i in range(count):
        now = bits(rng.choice((.1, 1.1, 13.37, 8192.3, 65536.125)))
        clocks = [0] * 3
        if i % 5 < 3: clocks[i % 3] = (0, now, bits(number(now) + 1))[i % 5]
        row = [i % 3, now, rng.choice((0, 1, 10, 75)), rng.randrange(2), *clocks,
            int(i % 11 == 0), int(i % 13 == 0), 0 if i % 7 == 0 else 0x40,
            rng.randrange(2), bits(1.25), bits(-2.5), bits(3.75), rng.choice((50, 55, 70)),
            rng.randrange(8), rng.randrange(1, 6), rng.choice((0, 20, 21, 38, 43))]
        cases.write(' '.join(f'{v:08x}' for v in row) + '\n')
        expected.write(' '.join(f'{v:08x}' for v in original.run(row)) + '\n')
    executed = {address for address in original.visited
        if mapper.BASE <= address < mapper.BASE + len(image) and address != 0x100DFF70}
    uncovered = sorted(address for address in executed if not any(start <= address < end for start, end in covered))
    if uncovered: raise ValueError('Original aim instruction outside proof: ' + ','.join(f'{v:08X}' for v in uncovered))
    return dict(sampleSha256=SHA, modelSha256=MODEL_SHA, proofSha256=sha(proof), cases=count,
        originalInstructions=len(executed), preeditCoverageComplete=True,
        controlledOriginalEntries=['100DFF70'], actualHelpers=['1000F390'],
        boundary='Complete original aim enter/exit/toggle with actual model duration; cold metadata values seeded, message/engine edges controlled. No full frame/SDK admission, deployment or gameplay.')
