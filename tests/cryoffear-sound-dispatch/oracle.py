# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the complete matched original PlaySFX sender and recipient lookup."""
import importlib.util
from pathlib import Path
import random
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_MXCSR

spec = importlib.util.spec_from_file_location('cof_original', Path(__file__).parents[1]/'cryoffear-ballistics/oracle.py')
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
Original, BASE, WORK, STACK, EDGE = shared.Original, shared.BASE, shared.WORK, shared.STACK, shared.EDGE
bits = shared.bits

SAMPLES = ('weapons_new/p345/p345_fire.wav', 'player/voice.wav', 'xweaponsfoo',
           'WEAPONS/foo.wav', 'weapons', 'guest/cof/weapons/p345/p345_click.wav',
           'a/weapons-b.wav', 'new/Weapons/x.wav')


class SoundSender(Original):
    def __init__(self, sample):
        super().__init__(sample)
        self.rules = WORK+0xc0000
        self.write(0x10222ee4, self.rules)
        self.write(self.rules, self.rules+0x1000)
        self.write(self.rules+0x1020, EDGE+0x1000)
        self.write(0x102266f4, 77)
        self.write(self.vtable(1)+0x228, EDGE+0x6000)
        self.write(0x10222f48+0x120, EDGE+0x120)

    def hook(self, uc, address, size, user):
        if address == 0x100125e0:  # Original engine Instance boundary.
            self.ret(0, self.obj(1))
            return
        if BASE <= address < BASE+0x300000:
            return super().hook(uc, address, size, user)
        sp = uc.reg_read(UC_X86_REG_ESP)
        arg = lambda n: self.read(sp+4+n*4)
        if address == EDGE+0x1000:
            self.ret(0, self.cooperative)
        elif address == EDGE+0x6000:
            self.events.extend([21, 0])
            for i, value in enumerate(self.gun):
                self.write(arg(0)+4*i, value)
            self.ret(4, arg(0))
        elif address == EDGE+0x120:
            self.ret(0, self.edict(arg(0)))
        elif address == EDGE+0x11c:
            self.ret(0, self.index(arg(0), self.edict))
        elif address == EDGE+0xb8:
            self.packet = dict(destination=arg(0), target=0 if not arg(3) else self.index(arg(3), self.edict),
                               bytes=[], coords=[], text=None)
            self.ret(0)
        elif address == EDGE+0xc0:
            self.packet['bytes'].append(arg(0) & 255)
            self.ret(0)
        elif address == EDGE+0xc8:
            self.packet['index'] = arg(0) & 65535
            self.ret(0)
        elif address == EDGE+0xd4:
            self.packet['coords'].append(arg(0))
            self.ret(0)
        elif address == EDGE+0xd8:
            text = bytes(uc.mem_read(arg(0), 180)).split(b'\0', 1)[0].decode('ascii')
            self.packet['text'] = SAMPLES.index(text)
            self.ret(0)
        elif address == EDGE+0xbc:
            p = self.packet
            assert len(p['bytes']) == 3 and len(p['coords']) == 3 and p['text'] is not None
            self.event(22, p['destination'], p['target'], p['text'], *p['bytes'][:2],
                       *p['coords'], p['bytes'][2], p['index'])
            self.ret(0)
        else:
            raise ValueError(f'Unexpected original sound edge {address:08x}')

    def run(self, row):
        # sample/rules/broadcast/mode/enabled/maxclients/missingmask/freeMask,
        # three original origin words, then three virtual gun-position words.
        sample, self.cooperative, broadcast, mode, enabled, maximum, absent, free = row[:8]
        self.gun = row[11:14]
        self.write(self.globals+0x90, maximum)
        for index in range(1, 6):
            self.write(self.edict(index), (free >> index) & 1)
            self.write(self.edict(index)+0x7c, 0 if absent & (1 << index) else self.obj(index))
        address = WORK+0xf0000
        self.uc.mem_write(address, SAMPLES[sample].encode('ascii')+b'\0')
        self.events, self.last = [], []
        stack = STACK+0x8000
        args = [0x60000000, address, mode, enabled, *row[8:11], self.obj(1), broadcast]
        for index, value in enumerate(args):
            self.write(stack+4*index, value)
        self.uc.reg_write(UC_X86_REG_ESP, stack)
        self.uc.reg_write(UC_X86_REG_ECX, self.obj(1))
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        try:
            self.uc.emu_start(0x10116a40, 0x60000000, count=20000)
        except Exception as error:
            raise RuntimeError(f'Original sender failed at {self.uc.reg_read(UC_X86_REG_EIP):08x}; row={row}; last={self.last}') from error
        if self.uc.reg_read(UC_X86_REG_EIP) != 0x60000000:
            raise ValueError('Original sender did not return')
        return self.events


def generate(sample, cases_file, expected_file):
    original = SoundSender(sample)
    randomizer = random.Random(0x10116a40)
    count = 0
    for name in range(len(SAMPLES)):
        for cooperative in (0, 1):
            for broadcast in (0, 1):
                for population in range(8):
                    maximum = (1, 3, 5)[population % 3]
                    row = [name, cooperative, broadcast, (0, 1, 2, 255, 256, 0xffffffff)[population % 6],
                           (0, 1, 128, 255)[population % 4], maximum,
                           (0, 4, 16, 28)[population % 4], (0, 8)[population % 2]]
                    row += [bits(randomizer.uniform(-100000, 100000)) for _ in range(6)]
                    # Source 1 remains valid; unavailable listeners are independent.
                    expected = original.run(row)
                    cases_file.write(' '.join(f'{v:08x}' for v in row)+'\n')
                    expected_file.write(' '.join(f'{v:08x}' for v in expected)+'\n')
                    count += 1
    required = (0x10116a7c, 0x10116ac8, 0x10116b47, 0x10116b4b,
                0x10116b94, 0x10116ba2, 0x10116c18, 0x10119698)
    if any(address not in original.visited for address in required):
        raise ValueError('Missing required original sender branch')
    return dict(sampleSha256=shared.SHA, functionVA='10116A40', functionRVA='116A40', cases=count,
                visitedInstructions=len(original.visited), requiredInstructions=[f'{a:08x}' for a in required],
                boundary='Original complete sender and recipient helper, actual scalar strstr instructions; controlled rule/Instance/gun-position and engine message boundaries')
