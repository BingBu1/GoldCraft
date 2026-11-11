# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute original P345 Fidget instructions, including both RNG calls.

The SDK comparison enters actual ItemPostFrame with unrelated common effects
quiet. Special states are rejected by the host preflight, matching no Fidget
effects without claiming their separate original lifecycle is implemented.
"""
from oracle import P345, WORK, STACK, EDGE, bits, shared
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_MXCSR


class Fidget(P345):
    def hook(self, uc, address, size, user):
        sp = uc.reg_read(UC_X86_REG_ESP)
        if address == EDGE + 0x168:
            low, high = self.read(sp + 4), self.read(sp + 8)
            value = self.chance if high == 599 else self.choice
            self.event(91, low, high, value)
            self.ret(0, value)
            return
        return super().hook(uc, address, size, user)

    def run(self, row):
        now, clip, primary, secondary, idle, action, special, aim, self.chance, self.choice = row
        self.events = []; self.last = []
        self.write(self.globals, now)
        fields = ((0x1e4, clip), (0x1cc, primary), (0x1d0, secondary),
                  (0x1d8, idle), (0x128, action), (0x140, special), (0x178, aim))
        for off, value in fields:
            self.write(self.weapon + off, value)
        self.uc.reg_write(UC_X86_REG_ESP, STACK + 0xff00)
        self.write(STACK + 0xff00, EDGE + 0xff00)
        self.uc.reg_write(UC_X86_REG_ECX, self.weapon)
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        self.uc.emu_start(0x1005da70, EDGE + 0xff00, count=2000)
        return [self.read(self.weapon + off) for off, _ in fields] + self.events


class CommonOrder(Fidget):
    """Run original common Post through input dispatch, retaining real Fidget.

    Wear and time return x87 floats, engine messages are observed calls, and
    inventory notification is a controlled boundary. No input action is run.
    """
    def __init__(self, sample):
        super().__init__(sample)
        self.write(WORK + 0xa0000 + 0x1d4, 0x1005da70)
        for off in (0x1bc, 0x1b8):
            self.write(WORK + 0xa0000 + off, EDGE + 0x7000 + off)
        self.uc.mem_write(EDGE + 0x71bc, bytes.fromhex('d905') + (WORK + 0xce000).to_bytes(4, 'little') + b'\xc3')
        self.write(WORK + 0xce000, bits(.75))
        self.uc.mem_write(EDGE + 0x7500, bytes.fromhex('d905') + self.globals.to_bytes(4, 'little') + b'\xc3')

    def hook(self, uc, address, size, user):
        if address == 0x1011d58f:
            self.event(94, self.read(self.weapon + 0x1b4))
        elif address == 0x1011d81b:
            self.event(95)
        elif address == 0x1011d9b6:
            self.event(98, self.read(self.weapon + 0x1e4), self.read(self.weapon + 0x1f0),
                       self.read(self.obj(1) + 0x2544))
            uc.emu_stop()
        elif address == EDGE + 0xb8:
            self.event(96)
            self.ret(0)
            return
        elif address == 0x100f5eb0:
            self.event(97, self.read(self.weapon + 0x1e4), self.read(self.weapon + 0x1f0),
                       self.read(self.obj(1) + 0x2544))
            self.ret(0)
            return
        elif address == EDGE + 0x71b8:
            self.ret(0)
            return
        elif EDGE + 0x71bc <= address < EDGE + 0x71c3 or EDGE + 0x7500 <= address < EDGE + 0x7507:
            return
        elif address == 0x1011ab20:
            uc.reg_write(shared.UC_X86_REG_EIP, EDGE + 0x7500)
            return
        return super().hook(uc, address, size, user)

    def check(self):
        rows = []
        for clip in (0, 1, 8):
            for loading in (0, 1):
                self.events = []; self.last = []
                self.chance, self.choice = 50, 1
                self.uc.mem_write(self.weapon + 8, b'\0' * 0x600)
                self.write(self.weapon + 0x160, self.obj(1))
                self.write(self.weapon + 0x168, 7)
                self.write(self.weapon + 0x1dc, 1)
                self.write(self.weapon + 0x1e4, clip)
                self.write(self.weapon + 0x1f0, loading)
                self.write(self.obj(1) + 0x2544, 39)
                self.write(self.obj(1) + 0x308, bits(10))
                self.write(0x102318a4 + 7 * 44, 8)
                self.write(0x102318ac + 7 * 44, 0)
                self.write(self.globals, bits(10))
                self.uc.reg_write(UC_X86_REG_ESP, STACK + 0xff00)
                self.write(STACK + 0xff00, EDGE + 0xff00)
                self.uc.reg_write(UC_X86_REG_ECX, self.weapon)
                self.uc.emu_start(0x1011d560, EDGE + 0xff00, count=5000)
                events, at = [], 0
                while at < len(self.events):
                    count = self.events[at + 1]
                    events.append(self.events[at:at + 2 + count])
                    at += count + 2
                kinds = [v[0] for v in events]
                wanted = [91, 91, 3, 94, 95, 96] + ([97] if loading else []) + [98]
                if kinds != wanted or events[3][2] != int(clip > 0):
                    raise ValueError(f'Original common Fidget order differs: {events}')
                if loading:
                    if events[-2][2:] != [8, 1, 39 - (8 - clip)] or events[-1][2:] != [8, 0, 39 - (8 - clip)]:
                        raise ValueError(f'Original common reload notification order differs: {events}')
                rows.append(dict(clip=clip, loading=loading, events=events))
        return rows


def generate(sample, cases, expected):
    original = Fidget(sample)
    count = 0

    def emit(row):
        nonlocal count
        cases.write(' '.join(f'{v:08x}' for v in row) + '\n')
        expected.write(' '.join(f'{v:08x}' for v in original.run(row)) + '\n')
        count += 1

    for now in (.1, 13.37, 8192.3):
        clock = bits(now)
        for clip in (0, 1, 8):
            for aiming in (0, 1):
                for chance in (49, 50, 51):
                    for choice in range(3):
                        emit([clock, clip, clock, clock, clock, 0, 0, aiming, chance, choice])
            # Test each strict lock independently, at previous/equal/next float.
            for which in range(3):
                for delta in (-1, 0, 1):
                    clocks = [clock] * 3
                    clocks[which] += delta
                    emit([clock, clip, *clocks, 0, 0, 0, 50, 2])
            for action, special in ((1, 0), (3, 0), (0, 1), (0, 666)):
                emit([clock, clip, clock, clock, clock, action, special, 0, 50, 1])
    return dict(sampleSha256=shared.SHA, originalFunction='1005DA70', cases=count,
                commonPostOrder=CommonOrder(sample).check(),
                boundary='Full original Fidget instructions and RNG/view edges; SDK enters actual ItemPostFrame; unsupported special lifecycle remains gated')
