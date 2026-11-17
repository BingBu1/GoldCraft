# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the complete original 870 fidget, recording both RNG calls and view."""
import importlib.util
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('shotgun_sdk_abi', ROOT / 'tests/cryoffear-magazine-actions/run.py')
abi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(abi)


class Original(abi.Original):
    def __init__(self, image):
        super().__init__(image, {})
        self.write(0x102230B0, abi.STUB + 0x30)

    def hook(self, uc, address, size, user):
        self.visited.add(address)
        sp = uc.reg_read(UC_X86_REG_ESP)
        if address == abi.STUB + 0x30:
            low, high = self.read(sp + 4), self.read(sp + 8)
            self.events += [low, high]
            uc.reg_write(UC_X86_REG_EAX, self.chance if high == 599 else self.choice)
            self.ret()
        elif address == abi.STUB:
            self.events.append(self.read(sp + 4))
            self.ret(12)
        elif not abi.BASE <= address < abi.BASE + 0x250000:
            raise ValueError(f'Unexpected external instruction {address:X}')

    def fidget(self, chance, choice, aim, gate):
        self.chance, self.choice, self.events = chance, choice, []
        self.write(abi.GLOBAL, abi.bits(10))
        for off in (0x1CC, 0x1D0, 0x1D8): self.write(abi.WEAPON + off, 0)
        for off in (0x128, 0x140): self.write(abi.WEAPON + off, 0)
        self.byte(abi.WEAPON + 0x178, aim)
        if gate: self.write(abi.WEAPON + 0x1D8, abi.bits(11))
        self.write(abi.STACK, abi.STOP)
        self.uc.reg_write(UC_X86_REG_ESP, abi.STACK)
        self.uc.reg_write(UC_X86_REG_ECX, abi.WEAPON)
        self.uc.emu_start(0x1006CC00, abi.STOP, count=1000)
        if self.uc.reg_read(UC_X86_REG_EIP) != abi.STOP:
            raise ValueError('Original fidget failed to return')
        return self.events


def generate(sample, cases, expected):
    runner = Original(abi.mapped_image(sample.read_bytes()))
    count = 0
    for chance in range(600):
        for choice in range(3):
            for aim in (0, 1):
                for gate in (0, 1):
                    row = (chance, choice, aim, gate)
                    result = runner.fidget(*row)
                    cases.write(' '.join(map(str, row)) + '\n')
                    expected.write(' '.join(map(str, result)) + '\n')
                    count += 1
    return dict(cases=count, originalInstructions=sorted(hex(at) for at in runner.visited
                if abi.BASE <= at < abi.BASE + 0x250000), controlled='Engine RNG and view transport only')
