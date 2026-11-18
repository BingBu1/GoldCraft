# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay complete original ordinary FAMAS PostFrame with callback mutations."""
import hashlib
import importlib.util
import json
from pathlib import Path
import random

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_MXCSR

ROOT = Path(__file__).resolve().parents[2]
SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
MODEL_SHA = '175cb022365adf25dc333b14b63dc7c1d6119817ea65f43f817acc6a7c631e54'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


shared = load('famas_original_frame', ROOT / 'tests/cryoffear-m16-frame/oracle.py')
mapper = load('famas_frame_model', ROOT / 'tests/cryoffear-magazine-actions/run.py')
WORK, STACK, EDGE = shared.WORK, shared.STACK, shared.EDGE
bits, number = shared.bits, shared.number


class Frame(shared.Frame):
    def __init__(self, sample, model):
        super().__init__(sample)
        start = mapper.u32(model, 168)
        self.uc.mem_write(self.model, model[:244]); self.write(self.model + 168, 244)
        self.uc.mem_write(self.model + 244, model[start:start + 24 * 176])
        self.write(0x1022302C, EDGE + 0x7480)
        self.uc.mem_write(EDGE + 0x7480, bytes.fromhex('d905') + (WORK + 0xCE000).to_bytes(4, 'little') + b'\xc3')
        self.visited = set()

    def snapshot(self):
        return super().snapshot() + [self.read(self.weapon + 0x1E4)]

    def observe(self, kind, arg=0):
        super().observe(kind, arg)
        if self.ordinal != self.nth: return
        if self.change & 64: self.write(self.weapon + 0x1E4, not self.read(self.weapon + 0x1E4))
        if self.change & 128: self.write(self.pev(1) + 0x184, self.read(self.pev(1) + 0x184) ^ 0x40)

    def hook(self, uc, address, size, user):
        self.visited.add(address)
        if EDGE + 0x7480 <= address <= EDGE + 0x7486: return
        return super().hook(uc, address, size, user)

    def run(self, row):
        (now, action, aim, pending, loading, primary, secondary, extra, idle, deadline,
         flags, vx, vy, condition, gate, gate_time, self.change, self.nth, self.delta,
         self.alt, clip, toggle, pressed, released, held) = row
        self.events, self.ordinal, self.last = [], 0, []
        for at, value in zip(shared.OFFSETS, (action, aim, pending, loading, primary, secondary, extra, idle, deadline)):
            self.write(self.weapon + at, value)
        self.write(self.weapon + 0x140, 0); self.write(self.globals, now); self.write(self.weapon + 0x1E4, clip)
        self.write(WORK + 0xCE000, bits(toggle))
        for at, value in ((0x1A8, flags), (0x20, vx), (0x24, vy), (0x21C, 0), (0x184, held)):
            self.write(self.pev(1) + at, value)
        for at, value in ((0x3AA, condition), (0x1EE8, gate), (0x1EE4, gate_time), (0x22B0, pressed), (0x22B4, released)):
            self.write(self.obj(1) + at, value)
        self.uc.reg_write(UC_X86_REG_ESP, STACK + 0xFF00)
        self.write(STACK + 0xFF00, EDGE + 0xFF00)
        self.uc.reg_write(UC_X86_REG_ECX, self.weapon); self.uc.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        self.uc.emu_start(0x1003B420, EDGE + 0xFF00, count=15000)
        if self.uc.reg_read(UC_X86_REG_EIP) != EDGE + 0xFF00: raise ValueError('Original frame failed to return')
        return self.snapshot() + self.events


def generate(sample, model, proof, cases, expected, timing):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    pre = json.loads(proof.read_text('utf-8-sig'))
    if sha(sample) != SHA or sha(model) != MODEL_SHA or not pre['beforeImplementation'] or pre['sampleSha256'] != SHA:
        raise ValueError('Original frame/pre-edit identities mismatch')
    image = mapper.mapped_image(sample.read_bytes()); covered = []
    for region in pre['regions']:
        start = int(region['va'], 16); data = bytes.fromhex(region['hex'])
        if not data or image[start-mapper.BASE:start-mapper.BASE+len(data)] != data or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Original frame bytes mismatch')
        covered.append((start, start + len(data)))
    for region in pre['constants']:
        start = int(region['va'], 16); data = bytes.fromhex(region['hex'])
        if image[start-mapper.BASE:start-mapper.BASE+len(data)] != data or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Original frame constant mismatch')
    for path, digest in pre['archives'].items():
        if sha(proof.parent / path) != digest: raise ValueError('Changed frame IDA evidence: ' + path)
    raw_model = model.read_bytes(); original = Frame(sample, raw_model); count = 0; rng = random.Random(432686)
    timing.write(' '.join(f'{v:08x}' for v in mapper.model_times(raw_model)[:24] + [0] * 19) + '\n')

    def emit(row):
        nonlocal count
        answer = original.run(row)
        cases.write(' '.join(f'{int(v):08x}' for v in row) + '\n')
        expected.write(' '.join(f'{v:08x}' for v in answer) + '\n'); count += 1

    for now in (.1, 13.37, 8192.3, 65536.125):
        for clip in (0, 1, 75):
            for action, flags, vx, vy, condition in ((0,0,0,0,0), (0,0x40000,3,4,0),
                    (1,0x40000,3,4,0), (1,0,0,0,0), (0,0,3,4,1), (5,0x40000,3,4,0), (2,0x40000,3,4,0)):
                for change in (0,1,2,3,4,5,8,9,16,17,32,33,64,65):
                    for nth in (1,2,3):
                        emit([bits(now),action,0,0,0,0,0,0,0,0,flags,bits(vx),bits(vy),condition,
                              0,0,change,nth,bits(.375),17,clip,1,0,0,0])
            for aim in (0,1):
                for gate in (0,1):
                    for toggle in (0,1):
                        emit([bits(now),0,aim,1,0,0,0,0,0,0,0x40000,bits(3),bits(4),0,
                              gate,0,129,1,bits(.375),17,clip,toggle,0,0,0])
            for pressed, released, held in ((0,0,0),(0x40,0,0),(0,0x40,0),(0,0,0x40)):
                emit([bits(now),0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,bits(.375),17,clip,0,pressed,released,held])
    for _ in range(1024):
        now = rng.choice((.1,13.37,8192.3,65536.125)); clock = lambda: bits(now+rng.choice((-1,0,1)))
        emit([bits(now),rng.choice((0,1,2,5)),rng.randrange(2),rng.randrange(2),rng.randrange(2),
              clock(),clock(),clock(),clock(),clock(),rng.choice((0,0x40000)),bits(rng.choice((0,3,.01))),
              bits(rng.choice((0,4,.02))),rng.randrange(2),rng.randrange(2),clock(),rng.randrange(256),
              rng.randrange(1,4),bits(.375),rng.choice((0,8,9,10,22,23,24,43)),rng.choice((0,1,75)),
              rng.randrange(2),rng.choice((0,0x40)),rng.choice((0,0x40)),rng.choice((0,0x40))])
    executed = {address for address in original.visited
        if mapper.BASE <= address < mapper.BASE + len(image) and address != 0x1011D560}
    uncovered = sorted(address for address in executed if not any(start <= address < end for start, end in covered))
    if uncovered: raise ValueError('Frame instruction outside pre-edit evidence: ' + ','.join(f'{v:08X}' for v in uncovered))
    return dict(cases=count, sampleSha256=SHA, modelSha256=MODEL_SHA, proofSha256=sha(proof),
        originalInstructions=len(executed), preeditCoverageComplete=True, controlledOriginalEntries=['1011D560'],
        boundary='Complete ordinary FAMAS derived frame; actual sequence duration/condition, controlled aim/reload/common frame. No special1/2, SDK admission, deployment or gameplay.')
