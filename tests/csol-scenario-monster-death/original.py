"""Bounded original death selector/deadline instructions vs production binary.

No full death owner, rewards, GameRules, native DLL loading, or gameplay.
CRT rand is fed the production target's observed first draw; only its call
count/selection is compared, not random streams across different CRT versions.
Map service is held stable. Appearance/behavior getters are outside the range.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import itertools
import json
import struct
import subprocess
import sys
sys.dont_write_bytecode = True
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EFLAGS, UC_X86_REG_MXCSR, UC_X86_REG_XMM0

helper = Path(__file__).parents[1] / 'csol-scenario/spawn_points_original.py'
spec = importlib.util.spec_from_file_location('death_binary_support', helper)
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)
ENTITY, PEV, BEHAVIOR, TYPE, SLOT, MANAGER, CLOCK, SERVICE, VTABLE = [0x20000000 + i * 0x2000 for i in range(9)]
STACK, STOP, RAND, GET_SERVICE, GET_MAP = 0x21008000, 0x30000000, 0x30000010, 0x30000020, 0x30000030
RANGES = [(0x113ee280, 0x113ee2f3), (0x1137d095, 0x1137d1ac)]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def identity(path):
    data = path.read_bytes()
    return {'path': str(path.resolve()), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


class OriginalDeath:
    original = support.Original.original

    def __init__(self, path, metadata):
        self.data = path.read_bytes()
        self.meta = json.loads(metadata.read_text(encoding='utf-8-sig'))
        assert hashlib.sha256(self.data).hexdigest() == support.EXPECTED_MP == self.meta['sha256']
        assert int(self.meta['image_base'], 16) == 0x10000000
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        for page in (0x113ee000, 0x1137d000, 0x11740000, 0x11791000, 0x11792000):
            self.u.mem_map(page, 0x1000)
            self.u.mem_write(page, self.original(page, 0x1000))
        # These two engine-global pages are PE zero-initialized .data, not
        # bytes in the file. Do not turn other missing bytes into zeroes.
        data_sections = [section for section in self.meta['sections'] if section['name'] == '.data']
        assert len(data_sections) == 1
        data_section = data_sections[0]
        raw_end = 0x10000000 + data_section['rva'] + data_section['raw_size']
        virtual_end = 0x10000000 + data_section['rva'] + data_section['virtual_size']
        image_end = 0x10000000 + int(self.meta['image_size'], 16)
        for page in (0x12278000, 0x12228000):
            assert raw_end <= page < page + 0x1000 <= virtual_end <= image_end
            self.u.mem_map(page, 0x1000)
            self.u.mem_write(page, bytes(0x1000))
        for address, size in [(ENTITY, 0x20000), (0x21000000, 0x10000), (STOP, 0x1000)]:
            self.u.mem_map(address, size)
        for address in [STOP, RAND, GET_SERVICE, GET_MAP, 0x1137d1ac]:
            self.u.hook_add(UC_HOOK_CODE, self.hook, begin=address, end=address)
        assert self.get(0x11792ae0) == bits(3)
        assert self.get(0x11791dc8) == bits(10)
        assert self.get(0x11791dc0) == bits(5)
        self.put(0x117404d4, RAND)
        self.put(0x12278834, GET_SERVICE)
        self.put(0x122788f4, CLOCK)
        self.put(0x12228e28, MANAGER)

    def put(self, address, value, size=4):
        self.u.mem_write(address, (value & ((1 << (size * 8)) - 1)).to_bytes(size, 'little'))

    def get(self, address, size=4):
        return int.from_bytes(self.u.mem_read(address, size), 'little')

    def return_value(self, value):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        self.u.reg_write(UC_X86_REG_EAX, value)
        self.u.reg_write(UC_X86_REG_EIP, self.get(esp))
        self.u.reg_write(UC_X86_REG_ESP, esp + 4)

    def hook(self, u, address, size, unused):
        if address in [STOP, 0x1137d1ac]:
            self.stopped = address
            u.emu_stop()
        elif address == RAND:
            self.random_calls += 1
            self.return_value(self.random_value)
        elif address == GET_SERVICE:
            self.service_calls += 1
            self.return_value(SERVICE)
        elif address == GET_MAP:
            assert u.reg_read(UC_X86_REG_ECX) == SERVICE
            self.map_calls += 1
            self.return_value(self.map_id)
        else:
            raise AssertionError(hex(address))

    def reset(self):
        self.u.mem_write(ENTITY, bytes(0x20000))
        self.u.mem_write(0x21000000, bytes(0x10000))
        for reg in [UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_XMM0]:
            self.u.reg_write(reg, 0)
        self.u.reg_write(UC_X86_REG_EFLAGS, 2)
        self.u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        self.u.reg_write(UC_X86_REG_ESP, STACK)
        self.put(STACK, STOP)
        self.stopped = None
        self.random_calls = self.service_calls = self.map_calls = 0

    def sequence(self, row, draw):
        _, present, hitgroup, seed, *sequences = row
        self.reset()
        self.random_value = draw
        self.put(BEHAVIOR + 0x1c, ENTITY)
        self.put(ENTITY + 0x12ec, TYPE if present else 0)
        self.put(ENTITY + 0x74, hitgroup)
        for i, sequence in enumerate(sequences):
            self.put(TYPE + 0x96 + 2 * i, sequence, 2)
        self.u.reg_write(UC_X86_REG_ECX, BEHAVIOR)
        self.u.emu_start(0x113ee280, 0, count=150)
        assert self.stopped == STOP and self.u.reg_read(UC_X86_REG_ESP) == STACK + 8
        result = self.u.reg_read(UC_X86_REG_EAX)
        return struct.unpack('<i', struct.pack('<I', result))[0], self.random_calls

    def deadline(self, row):
        _, now, duration, short_death, subtype, skill, priority, self.map_id = row
        self.reset()
        self.put(ENTITY + 8, PEV)
        self.put(ENTITY + 0x1348, short_death, 1)
        self.put(ENTITY + 0x12e8, SLOT)
        self.put(ENTITY + 0x12ec, TYPE)
        self.put(SLOT + 0x1a, skill, 2)
        self.put(TYPE + 0x1c, priority, 1)
        self.put(MANAGER + 0x1294, subtype)
        self.put(CLOCK, now)
        self.put(SERVICE, VTABLE)
        self.put(VTABLE + 0x194, GET_MAP)
        self.u.reg_write(UC_X86_REG_EDI, ENTITY)
        self.u.reg_write(UC_X86_REG_EBP, STACK + 0x100)
        self.put(STACK + 0x100 - 0x4c, duration)
        self.u.emu_start(0x1137d095, 0, count=250)
        assert self.stopped == 0x1137d1ac and self.u.reg_read(UC_X86_REG_ESP) == STACK
        assert self.get(ENTITY + 0x4c) == 0x1137d840
        assert self.service_calls == self.map_calls == (2 if priority else 0)
        return self.get(PEV + 0x114)


def main():
    parser = argparse.ArgumentParser()
    for name in ['exe', 'mp', 'metadata', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    assert not args.output.exists(), 'preserve prior execution evidence'
    rows = []
    variants = [[-32768, -3, -1, 0, 1, 32767, -9, 50, 100], list(range(100, 109))]
    for present, hitgroup, seed, values in itertools.product([0, 1], [1, 0, 2, -1, 7], [0, 1, 7, 42, 12345, 65535], variants):
        rows.append(['S', present, hitgroup, seed, *values])
    for now, duration, short, subtype, skill, priority, map_id in itertools.product([0.0, 20.0, 16777216.0], [0.0, 3.0, -30.0], [0, 1], [1, 10], [0, 11, 13, -32768], [0, 1, -128], [98, 361, 236]):
        rows.append(['D', bits(now), bits(duration), short, subtype, skill, priority, map_id])
    payload = ''.join(' '.join(map(str, row)) + '\n' for row in rows)
    run = subprocess.run([str(args.exe), '--oracle'], input=payload, text=True, capture_output=True, check=True)
    actual = [list(map(int, line.split())) for line in run.stdout.splitlines()]
    assert len(actual) == len(rows)
    original = OriginalDeath(args.mp, args.metadata)
    failures, selector_count, deadline_count = [], 0, 0
    for i, (row, output) in enumerate(zip(rows, actual)):
        if row[0] == 'S':
            selector_count += 1
            value, calls = original.sequence(row, output[2])
            expected_calls = int(bool(row[1]) and row[2] != 1)
            wanted = [value, output[3] if calls else output[2]]
            good = calls == expected_calls and output[:2] == wanted
        else:
            deadline_count += 1
            wanted = [1, original.deadline(row)]
            good = output == wanted
        if not good:
            failures.append({'index': i, 'input': row, 'actual': output, 'expected': wanted})
    report = {'scope': __doc__, 'checks': len(rows), 'selectorChecks': selector_count,
        'deadlineChecks': deadline_count, 'failures': failures,
        'inputs': [identity(path) for path in [args.exe, args.mp, args.metadata, Path(__file__), helper]],
        'ranges': [{'begin': hex(start), 'endExclusive': hex(end), 'bytes': end-start,
            'sha256': hashlib.sha256(original.original(start, end-start)).hexdigest()} for start, end in RANGES],
        'stdinSHA256': hashlib.sha256(payload.encode()).hexdigest(),
        'stdoutSHA256': hashlib.sha256(run.stdout.encode()).hexdigest(), 'stderr': run.stderr}
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'selector': selector_count, 'deadline': deadline_count, 'failures': len(failures)}))
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
