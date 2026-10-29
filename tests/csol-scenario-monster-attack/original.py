"""Compare original accepted-melee stage with production C++ using real CRT samples.

Original attack and sequence setter execute unchanged. The imported rand call
returns one value sampled independently from the target's CRT. ResetSequenceInfo
captures state; the C++ checks separately execute the pinned host SDK method.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import random
import struct
import subprocess
import sys
sys.dont_write_bytecode = True
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP,
    UC_X86_REG_EFLAGS, UC_X86_REG_MXCSR, UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2)

helper = Path(__file__).parents[1] / 'csol-scenario/spawn_points_original.py'
spec = importlib.util.spec_from_file_location('attack_binary_support', helper)
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)
ENTITY, PEV, BEHAVIOR, TYPE, ATTRIBUTES, CLOCK = [0x20000000 + i * 0x2000 for i in range(6)]
STACK, STOP, RAND = 0x21008000, 0x30000000, 0x30001000
CLOCK_OFFSETS = [0x1da8, 0x1dac, 0x1db0, 0x1db4, 0x1db8, 0x1dbc, 0x1d74, 0x1d78, 0x1d7c]
CONSTANTS = {0x117c2620: 0x43c80000, 0x1178f588: 0x3f000000,
    0x1178f58c: 0x3f800000, 0x117c25f8: 0x3ecccccd}
RANGES = [(0x113ebdf0, 0x113ebf36), (0x11383350, 0x113833ab)]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def signed(value, size):
    return int.from_bytes(value.to_bytes(size, 'little'), 'little', signed=True)


class OriginalAttack:
    original = support.Original.original

    def __init__(self, path, metadata):
        self.data = path.read_bytes()
        self.meta = json.loads(metadata.read_text(encoding='utf-8-sig'))
        assert hashlib.sha256(self.data).hexdigest() == support.EXPECTED_MP == self.meta['sha256']
        assert int(self.meta['image_base'], 16) == 0x10000000
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        pages = {0x113eb000, 0x11383000, 0x113f8000, 0x11740000}
        pages.update(address & ~0xfff for address in CONSTANTS)
        for page in pages:
            self.u.mem_map(page, 0x1000)
            self.u.mem_write(page, self.original(page, 0x1000))
        self.u.mem_map(ENTITY, 0x10000)
        self.u.mem_map(0x21000000, 0x10000)
        self.u.mem_map(0x12278000, 0x1000)
        self.u.mem_map(STOP, 0x2000)
        for address, value in CONSTANTS.items():
            assert self.get(address) == value
        self.put(0x117404d4, RAND)
        self.u.hook_add(UC_HOOK_CODE, self.stop, begin=STOP, end=STOP)
        self.u.hook_add(UC_HOOK_CODE, self.random, begin=RAND, end=RAND)
        self.u.hook_add(UC_HOOK_CODE, self.reset, begin=0x113f88f0, end=0x113f88f0)
        self.stopped = False
        self.resets = []
        self.random_calls = 0

    def put(self, address, value, size=4):
        self.u.mem_write(address, (value & ((1 << (size * 8)) - 1)).to_bytes(size, 'little'))

    def get(self, address, size=4):
        return int.from_bytes(self.u.mem_read(address, size), 'little')

    def clocks(self):
        return [self.get(BEHAVIOR + offset) for offset in CLOCK_OFFSETS]

    def state(self):
        return [signed(self.get(ENTITY + 0x1324, 2), 2), signed(self.get(PEV + 0x138), 4),
            self.get(PEV + 0x140), self.get(PEV + 0x148), self.get(BEHAVIOR + 0x1dd0)]

    def stop(self, u, address, size, unused):
        self.stopped = True
        u.emu_stop()

    def random(self, u, address, size, unused):
        esp = u.reg_read(UC_X86_REG_ESP)
        assert self.get(esp) == 0x113ebe29
        self.random_calls += 1
        assert self.random_calls == 1
        u.reg_write(UC_X86_REG_EAX, self.first_random)
        u.reg_write(UC_X86_REG_ESP, esp + 4)
        u.reg_write(UC_X86_REG_EIP, self.get(esp))

    def reset(self, u, address, size, unused):
        assert u.reg_read(UC_X86_REG_ECX) == ENTITY
        esp = u.reg_read(UC_X86_REG_ESP)
        assert self.get(esp) == 0x113833a6
        cache, sequence, frame, rate, lock = self.state()
        self.resets.append([sequence, cache, frame, rate, lock, *self.clocks()])
        self.put(PEV + 0x148, bits(1))
        u.reg_write(UC_X86_REG_ESP, esp + 4)
        u.reg_write(UC_X86_REG_EIP, self.get(esp))

    def case(self, row, draws):
        seed, cache, sequence, frame, rate, lock, now, x, y, interval, boosted, walk, run1, run2 = row
        self.first_random, second_random = draws
        self.u.mem_write(ENTITY, bytes(0x10000))
        self.u.mem_write(0x21000000, bytes(0x10000))
        for register in (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
            UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2):
            self.u.reg_write(register, 0)
        self.u.reg_write(UC_X86_REG_EFLAGS, 2)
        self.u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        self.u.reg_write(UC_X86_REG_ESP, STACK)
        self.put(STACK, STOP)
        self.put(ENTITY + 8, PEV)
        self.put(ENTITY + 0x1324, cache, 2)
        self.put(PEV + 0x138, sequence)
        self.put(PEV + 0x140, frame)
        self.put(PEV + 0x148, rate)
        self.put(BEHAVIOR + 0x1c, ENTITY)
        self.put(BEHAVIOR + 0x1dd0, lock)
        self.put(BEHAVIOR + 0x2b4, x)
        self.put(BEHAVIOR + 0x2b8, y)
        self.put(ENTITY + 0x12ec, TYPE)
        self.put(ENTITY + 0x12e8, ATTRIBUTES)
        self.put(ENTITY + 0x12d0, 0x7f7fffff if boosted else now)
        self.put(ATTRIBUTES + 0x14, interval)
        for offset, value in zip([0x80, 0x82, 0x84], [walk, run1, run2]):
            self.put(TYPE + offset, value, 2)
        for offset, value in zip(CLOCK_OFFSETS, [11, 12, 13, 21, 22, 23, 31, 32, 33]):
            self.put(BEHAVIOR + offset, bits(value))
        self.put(0x122788f4, CLOCK)
        self.put(CLOCK, now)
        self.u.reg_write(UC_X86_REG_ECX, BEHAVIOR)
        self.stopped, self.resets, self.random_calls = False, [], 0
        self.u.emu_start(0x113ebdf0, 0, count=250)
        assert self.stopped and self.u.reg_read(UC_X86_REG_EIP) == STOP
        assert self.u.reg_read(UC_X86_REG_ESP) == STACK + 4
        for register in (UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI):
            assert self.u.reg_read(register) == 0
        return [1, *self.state(), len(self.resets), *(value for call in self.resets for value in call),
            *self.clocks(), second_random if self.random_calls else self.first_random]


def cases():
    result = []
    moves = [(0, 0), (bits(20) - 1, 0), (bits(20), 0), (bits(20) + 1, 0),
        (bits(12), bits(16)), (bits(20), bits(1)), (bits(-21), 0), (1, 0),
        (0x00800000, 0), (0x80000000, 0), (bits(1e19), 0)]
    intervals = [bits(-3), 0x80000000, 0, bits(0.5), bits(0.9) - 1, bits(0.9), bits(0.9) + 1,
        bits(1), bits(1.5), bits(2)]
    for x, y in moves:
        for seed in [0, 1, 2, 3, 0x7fffffff, 0xffffffff]:
            for interval in intervals:
                for boosted in [0, 1]:
                    result.append((seed, 4, 4, bits(91), bits(2), bits(19), bits(25), x, y,
                        interval, boosted, 10, 11, 12))
    for slots in [(-1, -1, -1), (-3, -1, -1), (10, -1, -1), (-3, -3, -3),
        (4, 4, 4), (-32768, -32768, -32768), (32767, 32767, 32767), (10, -3, -1)]:
        for seed in range(16):
            for x in [0, bits(21)]:
                result.append((seed, 4, 4, bits(91), bits(2), bits(19), bits(25), x, 0,
                    bits(1.5), 0, *slots))
    rng = random.Random(0x113ebdf0)
    for _ in range(256):
        cache = rng.choice([4, 10, 11, 12])
        result.append((rng.randrange(0x100000000), cache, cache, bits(rng.uniform(0, 255)),
            bits(rng.uniform(0, 5)), bits(19), bits(rng.uniform(-1000, 100000)),
            bits(rng.uniform(-100, 100)), bits(rng.uniform(-100, 100)), bits(rng.uniform(-5, 10)),
            rng.randrange(2), 10, 11, 12))
    return list(dict.fromkeys(result))


def main():
    parser = argparse.ArgumentParser()
    for name in ['mp', 'metadata', 'exe', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    matrix = cases()
    seeds = list(dict.fromkeys(row[0] for row in matrix))
    sample = subprocess.run([str(args.exe.resolve()), '--random'], input='\n'.join(map(str, seeds)) + '\n',
        capture_output=True, text=True, timeout=45)
    draws = [list(map(int, line.split())) for line in sample.stdout.splitlines()]
    assert sample.returncode == 0 and len(draws) == len(seeds)
    assert all(len(pair) == 2 and all(0 <= value <= 32767 for value in pair) for pair in draws)
    by_seed = dict(zip(seeds, draws))
    original = OriginalAttack(args.mp, args.metadata)
    expected = [original.case(row, by_seed[row[0]]) for row in matrix]
    commands = [' '.join(map(str, row)) for row in matrix]
    result = subprocess.run([str(args.exe.resolve()), '--oracle'], input='\n'.join(commands) + '\n',
        capture_output=True, text=True, timeout=45)
    actual = [list(map(int, line.split())) for line in result.stdout.splitlines()]
    failures = [{'command': command, 'original': want, 'production': got}
        for command, want, got in zip(commands, expected, actual) if want != got]
    complete = result.returncode == 0 and len(actual) == len(expected)
    evidence = {'scope': 'accepted ordinary melee animation and three interval clocks only; finite no-overflow matrix',
        'cases': len(matrix), 'complete': complete, 'failures': failures,
        'returnCode': result.returncode, 'stderr': result.stderr,
        'crtSamples': [{'seed': seed, 'first': pair[0], 'second': pair[1]} for seed, pair in by_seed.items()],
        'ranges': [{'start': hex(start), 'endExclusive': hex(end),
            'sha256': hashlib.sha256(original.original(start, end - start)).hexdigest()} for start, end in RANGES],
        'instructionLimitIncludingSetter': 250,
        'constants': [{'address': hex(address), 'bits': hex(value), 'bytesHex': original.original(address, 4).hex()}
            for address, value in CONSTANTS.items()],
        'stopping': 'explicit STOP hook, EIP, exact ESP and restored EBP/EBX/ESI/EDI',
        'substitutes': ['controlled entity/pev/behavior/type/attribute/global fields; global time constant during each call',
            'IAT rand returns independently sampled first CRT value; successor stream checked in production output',
            'original ResetSequenceInfo capture with rate=1; host SDK body executes separately in C++ checks'],
        'notCovered': ['eligibility, target choice, damage, behavior-tree integration',
            'original CRT implementation/version equivalence, original ResetSequenceInfo full implementation',
            'ReAPI plugins, real model files, GameRules, native DLL deployment or live gameplay'],
        'originalSetterExecuted': True, 'nativeDllLoaded': False,
        'inputs': [support.evidence(path) for path in [args.mp, args.metadata, args.exe, Path(__file__), helper]]}
    args.output.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'cases': len(matrix), 'complete': complete, 'failures': len(failures)}))
    return 0 if complete and not failures else 1


if __name__ == '__main__':
    raise SystemExit(main())
