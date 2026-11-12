# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the original complete ScreenShake and fixed-word functions."""
import importlib.util
from pathlib import Path
import random
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_MXCSR

spec = importlib.util.spec_from_file_location('cof_sound_original', Path(__file__).parents[1]/'cryoffear-sound-dispatch/oracle.py')
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
BASE, WORK, STACK, EDGE = shared.BASE, shared.WORK, shared.STACK, shared.EDGE


class Shake(shared.Original):
    def __init__(self, sample):
        super().__init__(sample)
        self.write(0x1022671c, 77)
        self.write(0x10222f48+0x120, EDGE+0x120)

    def hook(self, uc, address, size, user):
        if BASE <= address < BASE+0x300000:
            return super().hook(uc, address, size, user)
        sp = uc.reg_read(UC_X86_REG_ESP)
        arg = lambda n: self.read(sp+4+n*4)
        if address == EDGE+0x120:
            self.ret(0, self.edict(arg(0)))
        elif address == EDGE+0xb8:
            self.packet = [arg(0), arg(1), self.index(arg(3), self.edict)]
            assert arg(2) == 0
            self.ret(0)
        elif address == EDGE+0xc8:
            self.packet.append(arg(0)&65535)
            self.ret(0)
        elif address == EDGE+0xbc:
            assert len(self.packet) == 6
            self.events.extend(self.packet)
            self.ret(0)
        else:
            raise ValueError(f'Unexpected original shake edge {address:08x}')

    def run(self, row):
        self.events, self.last = [], []
        self.write(self.globals+0x90, row[7])
        for i in range(1, 6):
            self.write(self.edict(i), (row[9]>>i)&1)
            self.write(self.edict(i)+0x7c, 0 if row[10]&(1<<i) else self.obj(i))
            self.write(self.pev(i)+0x1a8, 0x200 if row[8]&(1<<i) else 0)
            for axis in range(3):
                self.write(self.pev(i)+8+4*axis, row[11+3*(i-1)+axis])
        origin = WORK+0xe0000
        for axis in range(3):
            self.write(origin+4*axis, row[4+axis])
        stack = STACK+0x8000
        for i, word in enumerate([0x60000000, origin, *row[:4]]):
            self.write(stack+4*i, word)
        self.uc.reg_write(UC_X86_REG_ESP, stack)
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        self.uc.emu_start(0x10119de0, 0x60000000, count=3000)
        assert self.uc.reg_read(UC_X86_REG_EIP) == 0x60000000
        return self.events


def generate(sample, cases, expected):
    original = Shake(sample)
    rng = random.Random(0x10119de0)
    special = [shared.bits(x) for x in (0., -0., .1, .15, .25, 2., 5., -2., 16., 256., 1.e20)]
    special += [0x7f800000, 0xff800000, 0x7fc00001]
    for i in range(96):
        row = [special[i%len(special)], special[(i+2)%len(special)],
               special[(i+5)%len(special)], shared.bits((0., -1., 2., 10., float('nan'))[i%5]),
               *[shared.bits(0.)]*3, (1, 3, 5)[i%3], (62, 22, 0)[i%3],
               (0, 8)[i%2], (0, 16)[i%2]]
        for player in range(5):
            # Include exact and neighboring radius boundaries, plus true 3D distances.
            position = ((0., 0., 0.), (2., 0., 0.), (1.99999988, 0., 0.),
                        (0., 0., 10.), tuple(rng.uniform(-5, 5) for _ in range(3)))[player]
            row += [shared.bits(x) for x in position]
        result = original.run(row)
        cases.write(' '.join(f'{v:08x}' for v in row)+'\n')
        expected.write(' '.join(f'{v:08x}' for v in result)+'\n')
    required = {0x10119e5b, 0x10119e72, 0x10119ec8, 0x10119ed9, 0x10119f06,
                0x101166df, 0x10119698}
    assert required <= original.visited
    return dict(sampleSha256=shared.shared.SHA, cases=96, functions=['10119DE0', '101166D0', '10119680'],
                requiredInstructions=[f'{x:08x}' for x in sorted(required)])
