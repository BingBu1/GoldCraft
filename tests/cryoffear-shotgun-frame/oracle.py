# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay complete original 870 aim/idle/ordinary derived-frame instructions."""
import hashlib
import importlib.util
import json
from pathlib import Path
import random

from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP,
                               UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_MXCSR)

ROOT = Path(__file__).resolve().parents[2]
SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
MODEL_SHA = '08ad6ab4a5fe3156b2577a795aa00a9b1e3afe5c6c96ae3558f41b5f5e6f15f6'
spec = importlib.util.spec_from_file_location('shotgun_frame_abi', ROOT / 'tests/cryoffear-magazine-actions/run.py')
abi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(abi)
bits = abi.bits


def number(value):
    import struct
    return struct.unpack('<f', struct.pack('<I', value))[0]


W, O, WP, OP, G = abi.WEAPON, abi.OWNER, abi.WPEV, abi.OPEV, abi.GLOBAL
SNAPSHOT = [(W, 0x1E4), (O, 0x2544), (W, 0x128), (W, 0x140), (W, 0x1C8),
            (WP, 0x248), (WP, 0x24C), (W, 0x178), (W, 0x1A4),
            *[(W, at) for at in (0x1CC, 0x1D0, 0x1D4, 0x1D8)],
            (O, 0x308), (O, 0x4F4), (W, 0x1FC), (W, 0x1F8), (W, 0x12C),
            (G, 0), (OP, 0x274), (OP, 0x21C), (OP, 0x1A8), (OP, 0x20), (OP, 0x24)]


class Original(abi.Original):
    def __init__(self, image, model):
        super().__init__(image, {})
        self.uc.mem_write(abi.MODEL, model)
        self.write(abi.VTABLE + 8, abi.STUB + 0x30)
        self.write(abi.VTABLE + 0x12C, 0x1006D0F0)
        self.write(abi.VTABLE + 0x130, 0x1006CE20)
        self.write(abi.VTABLE + 0x1C0, 0x1006E3E0)
        self.write(0x10226668, 93)
        self.write(0x102265CC, 94)
        for offset, stub in ((0xB8, 0x100), (0xBC, 0x110), (0xD4, 0x120), (0xC0, 0x130)):
            self.write(0x10222F48 + offset, abi.STUB + stub)
        self.write(0x1022302C, abi.STUB + 0x200)
        self.uc.mem_write(abi.STUB + 0x200, bytes.fromhex('d905') + (G + 0x200).to_bytes(4, 'little') + b'\xc3')

    def snapshot(self):
        return [self.byte(base + off) if (base, off) in ((W, 0x178), (W, 0x1A4))
                else self.read(base + off) for base, off in SNAPSHOT]

    def observe(self, kind, *values):
        self.events += [kind, len(values) + len(SNAPSHOT), *values, *self.snapshot()]
        self.ordinal += 1
        if self.ordinal != self.nth:
            return
        if self.change & 1: self.write(G, bits(number(self.read(G)) + self.delta))
        if self.change & 2: self.write(OP + 0x1A8, self.read(OP + 0x1A8) ^ 0x40000)
        if self.change & 4:
            self.write(OP + 0x20, 0); self.write(OP + 0x24, 0)
        if self.change & 8: self.write(OP + 0x21C, self.alt)
        if self.change & 16: self.write(W + 0x1E4, int(not self.read(W + 0x1E4)))
        if self.change & 32: self.write(WP + 0x248, int(not self.read(WP + 0x248)))
        if self.change & 64: self.write(WP + 0x24C, int(not self.read(WP + 0x24C)))

    def hook(self, uc, address, size, user):
        if abi.STUB + 0x200 <= address <= abi.STUB + 0x206:
            return
        self.visited.add(address)
        sp = uc.reg_read(UC_X86_REG_ESP)
        arg = lambda n: self.read(sp + 4 + n * 4)
        if address == abi.STUB:
            sequence = arg(0)
            self.observe(0, sequence)
            self.write(OP + 0x21C, sequence)
            self.ret(12)
        elif address == abi.STUB + 0x20:
            uc.reg_write(UC_X86_REG_EAX, abi.MODEL); self.ret()
        elif address == abi.STUB + 0x30:
            self.observe(13, 0, arg(0)); self.ret(4)
        elif address == abi.STUB + 0x100:
            self.message, self.fields = arg(1), []
            self.ret()
        elif address in (abi.STUB + 0x120, abi.STUB + 0x130):
            self.fields.append(arg(0)); self.ret()
        elif address == abi.STUB + 0x110:
            assert self.message in (93, 94)
            assert len(self.fields) == (5 if self.message == 93 else 3)
            self.observe(1 if self.message == 93 else 2, *self.fields)
            self.ret()
        elif address == 0x100DFF70:
            # Cold immutable metadata is already seeded, just as in the host.
            self.ret(8); uc.reg_write(UC_X86_REG_EAX, G + 0x300)
        elif address in (0x1006D360, 0x1006D071):
            self.observe(3, self.read(OP + 0x274))
        elif address == 0x1000F390:
            self.observe(4, arg(1))  # Execute actual original duration afterwards.
        elif address == 0x100DFEA0:
            self.observe(5, arg(1))  # Execute the original forward-copy helper.
        elif address == 0x1006CE20 and self.operation == 4:
            self.observe(6)
        elif address == 0x1006E3E0:
            self.observe(7)  # Do not stub the reload body or its side effects.
        elif address == 0x1011D560:
            self.observe(8); self.ret()
        elif address == 0x100EA240:
            label = bytes(uc.mem_read(arg(0), 64)).split(b'\0', 1)[0]
            names = {b'shotgun_reload_start': 1, b'shotgun_reload_insert': 2,
                     b'shotgun_reload_endempty': 6, b'shotgun_reloadend': 7,
                     b'shotgun_reload_end': 8}
            if label not in names: raise ValueError('Unexpected player animation: ' + repr(label))
            self.observe(10 + names[label], 0, 0); self.ret(8)
        elif not abi.BASE <= address < abi.BASE + len(self.image):
            raise ValueError(f'Unexpected external instruction {address:08X}')

    def run(self, row):
        (self.operation, now, clip, reserve, action, special, stage, empty, abort, aim, pending,
         primary, secondary, extra, idle, attack, animation, insert, gate, deadline, fov, sequence,
         flags, vx, vy, condition, pending_gate, gate_time, pressed, toggle, x, y, z, zoom,
         self.change, self.nth, delta, self.alt) = row
        self.events, self.ordinal, self.delta = [], 0, number(delta)
        values = [clip, reserve, action, special, stage, empty, abort, aim, pending, primary,
                  secondary, extra, idle, attack, animation, insert, gate, deadline, now,
                  fov, sequence, flags, vx, vy]
        for (base, offset), value in zip(SNAPSHOT, values):
            self.write(base + offset, value)
        for at, value in ((0x17C, x), (0x180, y), (0x184, z), (0x188, zoom)):
            self.write(W + at, value)
        self.write(O + 0x3AA, condition); self.write(O + 0x1EE8, pending_gate)
        self.write(O + 0x1EE4, gate_time); self.write(O + 0x22B0, pressed)
        self.write(G + 0x200, bits(toggle))
        self.write(abi.STACK, abi.STOP)
        self.uc.reg_write(UC_X86_REG_ESP, abi.STACK)
        self.uc.reg_write(UC_X86_REG_ECX, W)
        self.uc.reg_write(UC_X86_REG_FPCW, 0x27F)
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        self.uc.emu_start((0x1006D0F0, 0x1006CE20, 0x1006EB70, 0x1006EC30, 0x1006D3E0)[self.operation],
                          abi.STOP, count=12000)
        if self.uc.reg_read(UC_X86_REG_EIP) != abi.STOP:
            raise ValueError('Complete original function did not return')
        return self.snapshot() + self.events


def generate(sample, model, proof, cases, expected, timing):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    pre = json.loads(proof.read_text('utf-8-sig'))
    if sha(sample) != SHA or sha(model) != MODEL_SHA or not pre['beforeImplementation'] or pre['sampleSha256'] != SHA:
        raise ValueError('Original/metadata/pre-edit identity mismatch')
    image = abi.mapped_image(sample.read_bytes())
    covered = []
    for region in pre['regions']:
        start, size = int(region['va'], 16), region['size']
        if hashlib.sha256(image[start - abi.BASE:start - abi.BASE + size]).hexdigest() != region['sha256']:
            raise ValueError('Changed original function: ' + region['va'])
        covered.append((start, start + size))
    for name, digest in pre['archives'].items():
        if sha(proof.parent / name) != digest: raise ValueError('Changed IDA archive: ' + name)
    control_path = proof.parent / '../PREEDIT-control.json'
    control = json.loads(control_path.read_text('utf-8-sig'))
    if not control['beforeImplementation'] or control['sampleSha256'] != SHA:
        raise ValueError('Missing original reload proof')
    for name, digest in control['archives'].items():
        if sha(control_path.parent / name) != digest: raise ValueError('Changed reload IDA archive: ' + name)
    for region in control['regions'] + control['additionalRegions']:
        start, size = int(region['va'], 16), region['size']
        if hashlib.sha256(image[start - abi.BASE:start - abi.BASE + size]).hexdigest() != region['sha256']:
            raise ValueError('Changed original reload helper')
        covered.append((start, start + size))
    machine = Original(image, model.read_bytes()); machine.image = image
    timing.write(' '.join(f'{v:08x}' for v in abi.model_times(model.read_bytes())[:20]) + '\n')
    rng, count = random.Random(87086), 0

    def initial(op=4, now=13.37):
        return [op, bits(now), 1, 50, 0, 0, 0, 0, 0, 0, 0, *([0] * 9), bits(90), 0,
                0, 0, 0, 0, 0, 0, 0x40, 1, bits(1.25), bits(-2.5), bits(3.75), 55,
                0, 1, bits(.375), 17]

    def emit(row):
        nonlocal count
        if len(row) != 38: raise ValueError('Wrong case width')
        answer = machine.run(row)
        cases.write(' '.join(f'{int(v) & 0xFFFFFFFF:08x}' for v in row) + '\n')
        expected.write(' '.join(f'{v:08x}' for v in answer) + '\n'); count += 1

    for op in range(5):
        for i in range(768):
            now = rng.choice((.1, 13.37, 8192.3, 65536.125))
            row = initial(op, now)
            row[2], row[3] = rng.choice((0, 1, 4, 5, 75, 511)), rng.choice((0, 1, 50))
            row[4], row[5] = rng.choice((0, 0, 0, 1, 2, 5)), int(op < 4 and i % 17 == 0)
            row[6:11] = [rng.choice((0, 0, 1, 2)), rng.randrange(2), rng.randrange(2), rng.randrange(2), rng.randrange(2)]
            for at in (11, 12, 13, 14, 19): row[at] = rng.choice((0, bits(now), bits(now + 1)))
            row[18], row[22] = rng.randrange(2), rng.choice((0, 0x40000))
            row[23:30] = [bits(rng.choice((0, 3, .01))), bits(rng.choice((0, 4, .02))),
                          rng.randrange(2), rng.randrange(2), 0, rng.choice((0, 1, 0x40)), rng.randrange(2)]
            row[34:36] = [rng.randrange(128), rng.randrange(1, 8)]
            row[37] = rng.choice((0, 8, 9, 10, 14, 17, 20, 43))
            emit(row)
    # Guaranteed pending-aim/reload, all finish paths and live movement reads.
    for now in (.1, 13.37, 8192.3, 65536.125):
        for scenario in range(10):
            for change in (0, 1, 2, 3, 4, 5, 8, 9, 16, 17, 32, 33, 64, 65):
                for nth in range(1, 6):
                    row = initial(4, now); row[34:36] = [change, nth]
                    if scenario < 2: row[9:11] = [scenario, 1]
                    elif scenario < 6:
                        row[4], row[6], row[2] = 2, 1, 5
                        row[7], row[8] = scenario & 1, scenario >> 2
                    elif scenario == 6: row[6], row[4] = 1, 2
                    elif scenario == 7: row[25] = 1
                    elif scenario == 8: row[22:25] = [0x40000, bits(3), bits(4)]
                    else: row[4], row[22:25] = 1, [0, 0, 0]
                    emit(row)
    # Metadata/time constants and helper regions already verified for control01
    # are included in the new proof before execution, not learned from failures.
    controlled = {0x100DFF70, 0x100EA240, 0x1011D560}
    executed = {a for a in machine.visited if abi.BASE <= a < abi.BASE + len(image) and a not in controlled}
    uncovered = sorted(a for a in executed if not any(lo <= a < hi for lo, hi in covered))
    if uncovered: raise ValueError('Instruction outside pre-edit evidence: ' + ','.join(f'{a:08X}' for a in uncovered))
    return dict(cases=count, sampleSha256=SHA, modelSha256=MODEL_SHA, proofSha256=sha(proof),
                originalInstructions=len(executed), preeditCoverageComplete=True,
                functions=['1006D0F0', '1006CE20', '1006EB70', '1006EC30', '1006D3E0', '1006E3E0'],
                controlledOriginalEntries=[f'{a:08X}' for a in sorted(controlled)],
                boundary='Complete original aim/idle/ordinary derived frame with actual reload/duration/condition; '
                         'cold metadata, engine effects and common frame are explicit controlled edges. No SDK/live acceptance.')
