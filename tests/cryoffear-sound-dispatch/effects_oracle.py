# SPDX-License-Identifier: GPL-3.0-or-later
"""Complete original shell sender and cooperative view-recoil instruction replay."""
import random

from oracle import Original, BASE, WORK, STACK, EDGE, bits, shared
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_MXCSR


class EffectsSender(Original):
    def __init__(self, sample):
        super().__init__(sample)
        self.write(0x10222f48 + 0xd0, EDGE + 0xd0)  # Actual original WriteAngle slot.
        self.write(0x10223134, EDGE + 0x7000)  # PlaybackEvent.
        rules = WORK + 0xc0000
        self.write(0x10222ee4, rules)
        self.write(rules, rules + 0x1000)
        self.write(rules + 0x1020, EDGE + 0x7004)

    def hook(self, uc, address, size, user):
        if BASE <= address < BASE + 0x300000:
            return super().hook(uc, address, size, user)
        sp = uc.reg_read(UC_X86_REG_ESP)
        arg = lambda n: self.read(sp + 4 + n * 4)
        if address == EDGE + 0xb8:
            self.event(31, arg(0), arg(1), int(arg(2) != 0),
                       *(self.vector(arg(2)) if arg(2) else [0, 0, 0]),
                       self.index(arg(3), self.edict) if arg(3) else 0)
            self.ret(0)
        elif address in (EDGE + 0xc0, EDGE + 0xc8, EDGE + 0xd0, EDGE + 0xd4):
            kind = {EDGE + 0xc0: 32, EDGE + 0xc8: 35,
                    EDGE + 0xd0: 34, EDGE + 0xd4: 33}[address]
            value = arg(0)
            if kind == 32:
                value &= 255
            if kind == 35:
                value &= 65535
            self.event(kind, value)
            self.ret(0)
        elif address == EDGE + 0xbc:
            self.event(36)
            self.ret(0)
        elif address == EDGE + 0x7004:
            self.ret(0, 1)  # Explicitly the cooperative rules branch only.
        elif address == EDGE + 0x7000:
            self.event(37, arg(0), self.index(arg(1), self.edict), arg(2), arg(3),
                       *self.vector(arg(4)), *self.vector(arg(5)),
                       *(arg(i) for i in range(6, 12)))
            self.ret(0)
        else:
            raise ValueError(f'Unexpected original effect edge {address:08x}')

    def run(self, row):
        kind, *values = row
        self.events, self.last = [], []
        self.write(0x10231f44, values[9])
        first, second = WORK + 0xe0000, WORK + 0xe0100
        for i in range(3):
            self.write(first + 4*i, values[i])
            self.write(second + 4*i, values[i + 3])
        stack = STACK + 0x8000
        args = ([0x60000000, first, second, *values[6:9]] if kind == 0
                else [0x60000000, *values[:3]])
        for i, value in enumerate(args):
            self.write(stack + 4*i, value)
        self.uc.reg_write(UC_X86_REG_ESP, stack)
        self.uc.reg_write(UC_X86_REG_ECX, self.obj(1))
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        start = 0x1011cf70 if kind == 0 else 0x100f91b0
        self.uc.emu_start(start, 0x60000000, count=3000)
        if self.uc.reg_read(UC_X86_REG_EIP) != 0x60000000:
            raise ValueError('Original effect sender did not return')
        return self.events


def generate(sample, cases, expected):
    original = EffectsSender(sample)
    rng = random.Random(0x1011cf70)
    count = 0
    angles = (0.0, -0.0, 360.0, -720.125, 1e20, -1e20, 3.4028234663852886e38)
    for kind in (0, 1):
        for index in range(64):
            row = [kind, *(bits(rng.uniform(-200000, 200000)) for _ in range(6)),
                   bits(angles[index % len(angles)]), (index * 991 % 65535) + 1,
                   (0, 1, 255, 256, 0xffffffff)[index % 5], (index * 127 % 65535) + 1]
            result = original.run(row)
            cases.write(' '.join(f'{value:08x}' for value in row) + '\n')
            expected.write(' '.join(f'{value:08x}' for value in result) + '\n')
            count += 1
    required = (0x1011cf7e, 0x1011cffa, 0x1011d014, 0x100f91c5, 0x100f9224)
    if not set(required) <= original.visited:
        raise ValueError('Original effect instructions were not reached')
    return dict(sampleSha256=shared.SHA, cases=count, functions=['1011CF70', '100F91B0'],
                requiredInstructions=[f'{address:08x}' for address in required],
                boundary='Complete shell helper and cooperative recoil branch; native message/event edges controlled; no original engine rendering')
