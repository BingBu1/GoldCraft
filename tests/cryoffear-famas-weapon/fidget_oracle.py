# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute complete original FAMAS Fidget, not a reimplementation of its rules."""
import importlib.util
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_MXCSR

spec = importlib.util.spec_from_file_location(
    'cof_famas_shared_oracle', Path(__file__).parents[1] / 'cryoffear-vp70-weapon/oracle.py')
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)


class Fidget(shared.VP70):
    def hook(self, uc, address, size, user):
        if address == shared.EDGE + 0x168:
            sp = uc.reg_read(UC_X86_REG_ESP)
            low, high = self.read(sp + 4), self.read(sp + 8)
            value = self.chance if high == 599 else self.choice
            self.event(91, low, high, value)
            self.ret(0, value)
            return
        return super().hook(uc, address, size, user)

    def run(self, row):
        now, clip, primary, secondary, idle, action, special, aim, self.chance, self.choice = row
        self.events = []
        self.last = []
        self.write(self.globals, now)
        fields = ((0x1e4, clip), (0x1cc, primary), (0x1d0, secondary),
                  (0x1d8, idle), (0x128, action), (0x140, special), (0x178, aim))
        for off, value in fields:
            self.write(self.weapon + off, value)
        self.uc.reg_write(UC_X86_REG_ESP, shared.STACK + 0xff00)
        self.write(shared.STACK + 0xff00, shared.EDGE + 0xff00)
        self.uc.reg_write(UC_X86_REG_ECX, self.weapon)
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        self.uc.emu_start(0x1003ace0, shared.EDGE + 0xff00, count=2000)
        return [self.read(self.weapon + off) for off, _ in fields] + self.events


def generate(sample, cases, expected):
    original = Fidget(sample)
    count = 0

    def emit(row):
        nonlocal count
        cases.write(' '.join(f'{v:08x}' for v in row) + '\n')
        expected.write(' '.join(f'{v:08x}' for v in original.run(row)) + '\n')
        count += 1

    for now in (.1, 13.37, 8192.3):
        clock = shared.bits(now)
        for clip in (0, 1, 30, 75):
            for aiming in (0, 1):
                for chance in (49, 50, 51):
                    for choice in range(3):
                        emit([clock, clip, clock, clock, clock, 0, 0, aiming, chance, choice])
            for which in range(3):
                for delta in (-1, 0, 1):
                    clocks = [clock] * 3
                    clocks[which] += delta
                    emit([clock, clip, *clocks, 0, 0, 0, 50, 2])
            for action, special in ((1, 0), (2, 0), (3, 0), (5, 0), (0, 1), (0, 666)):
                emit([clock, clip, clock, clock, clock, action, special, 0, 50, 1])
    return dict(sampleSha256=shared.shared.SHA, originalFunction='1003ACE0', cases=count,
                boundary='Full original Fidget instructions; real SDK method, controlled RNG/view delivery')
