# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the complete original ID23 reload branch and real weapon clock."""
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import struct

from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('famas_reload_abi', ROOT / 'tests/cryoffear-magazine/run.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
SHA = base.SAMPLE_SHA


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


class Original(base.Original):
    def __init__(self, mapped):
        super().__init__(mapped)
        self.write(base.WEAPON + 0x168, 23)
        self.visited = set()

    def write(self, address, value):
        super().write(address, value & 0xFFFFFFFF)

    def hook(self, uc, address, size, user):
        self.visited.add(address)
        if address == base.NOTIFY:
            raise ValueError('Original FAMAS must not use the ordinary reload observer')

    def run(self, row):
        clip, reserve, capacity, flags, now, deadline, loading = row
        for at, value in ((base.WEAPON + 0x1E4, clip), (base.PLAYER + 0x2544, reserve),
                          (base.WEAPON + 0x1F0, loading), (base.PLAYER + 0x308, deadline),
                          (base.GLOBALS, now), (0x102318A4 + 23 * 44, capacity),
                          (0x102318AC + 23 * 44, flags)):
            self.write(at, value)
        self.uc.reg_write(UC_X86_REG_EDI, base.WEAPON)
        self.uc.reg_write(UC_X86_REG_EDX, base.PLAYER)
        self.uc.reg_write(UC_X86_REG_EBP, base.STACK + 0x8000)
        self.uc.reg_write(UC_X86_REG_ESP, base.STACK + 0x7000)
        self.uc.emu_start(base.START, base.STOP, count=100)
        if self.uc.reg_read(UC_X86_REG_EIP) != base.STOP:
            raise ValueError('Original FAMAS reload did not reach its inspected boundary')
        actual_clip = self.read(base.WEAPON + 0x1E4)
        actual_loading = self.read(base.WEAPON + 0x1F0)
        committed = bool(loading and not actual_loading)
        return [actual_clip, self.read(base.PLAYER + 0x2544), actual_loading,
                int(committed), actual_clip - clip if committed else 0]


def generate(sample, proof, cases, expected):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    pre = json.loads(proof.read_text('utf-8-sig'))
    if sha(sample) != SHA or pre['sampleSha256'] != SHA or not pre['beforeImplementation']:
        raise ValueError('Original FAMAS reload identity mismatch')
    mapped = base.image(sample.read_bytes())
    covered = []
    for region in pre['regions']:
        address = int(region['va'], 16); data = bytes.fromhex(region['hex'])
        if mapped[address-base.BASE:address-base.BASE+len(data)] != data or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Original FAMAS reload region mismatch')
        covered.append((address, address + len(data)))
    for name, digest in pre['archives'].items():
        if sha(proof.parent / name) != digest:
            raise ValueError('Original FAMAS reload archive changed')
    original = Original(mapped); count = 0
    for clip, reserve, flags, loading, offset in itertools.product(
            range(31), (-2147483648, 0, 1, 150, 2147483647), (0, 0x20, 0x80000000), (0, 1), (-1, 0, 1)):
        deadline = bits(10.25)
        row = [clip, reserve, 30, flags, deadline + offset, deadline, loading]
        cases.write(' '.join(f'{v & 0xFFFFFFFF:08x}' for v in row) + '\n')
        expected.write(' '.join(f'{v & 0xFFFFFFFF:08x}' for v in original.run(row)) + '\n')
        count += 1
    executed = {a for a in original.visited if a != base.STOP}
    uncovered = [a for a in executed if not any(start <= a < end for start, end in covered)]
    if uncovered:
        raise ValueError('Original reload instruction outside pre-edit evidence')
    return dict(sampleSha256=SHA, proofSha256=sha(proof), cases=count,
        originalInstructions=len(executed), preeditCoverageComplete=True, originalId=23,
        actualRegisteredCapacity=30, ordinaryOwnerNotifications=0,
        boundary='Complete original FAMAS reload-completion branch and clock helper; not full common/derived frame, SDK, deployment, gameplay or performance acceptance.')
