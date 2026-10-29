"""Bounded original setter and ordinary locomotion against the production SDK target.

Original ResetSequenceInfo is a capture substitute. Original setter, selection,
float constants, sqrt, stores and climb getter remain original instructions.
The C++ target separately executes the host SDK reset body against a model.
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
    UC_X86_REG_EFLAGS, UC_X86_REG_MXCSR, UC_X86_REG_XMM0, UC_X86_REG_XMM1)

helper = Path(__file__).parents[1] / 'csol-scenario/spawn_points_original.py'
spec = importlib.util.spec_from_file_location('animation_binary_support', helper)
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)
ENTITY, PEV, BEHAVIOR, TYPE, VTABLE, CLOCK = [0x20000000 + i * 0x2000 for i in range(6)]
STACK, STOP = 0x21008000, 0x30000000
SEQUENCE_OFFSETS = [0x76, 0x7a, 0x7c, 0x86, 0x88, 0x8a, 0xb6]
CONSTANTS = {0x117c2620: 0x43c80000, 0x11794490: 0x42100000,
    0x117c25e8: 0x3cf5c290, 0x117c25f0: 0x3dcbfb16, 0x117c25f8: 0x3ecccccd}
RANGES = [(0x11383350, 0x113833ab), (0x113f1af0, 0x113f1c47), (0x107b6560, 0x107b6567)]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def signed16(value):
    return struct.unpack('<h', struct.pack('<H', value & 0xffff))[0]


def signed32(value):
    return struct.unpack('<i', struct.pack('<I', value & 0xffffffff))[0]


class OriginalAnimation:
    original = support.Original.original

    def __init__(self, path, metadata):
        self.data = path.read_bytes()
        self.meta = json.loads(metadata.read_text(encoding='utf-8-sig'))
        assert hashlib.sha256(self.data).hexdigest() == support.EXPECTED_MP == self.meta['sha256']
        assert int(self.meta['image_base'], 16) == 0x10000000
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        pages = {0x11383000, 0x113f1000, 0x107b6000, 0x113f8000}
        pages.update(address & ~0xfff for address in CONSTANTS)
        for page in pages:
            self.u.mem_map(page, 0x1000)
            self.u.mem_write(page, self.original(page, 0x1000))
        self.u.mem_map(ENTITY, 0x10000)
        self.u.mem_map(0x21000000, 0x10000)
        self.u.mem_map(0x12278000, 0x1000)
        self.u.mem_map(STOP, 0x1000)
        for address, value in CONSTANTS.items():
            assert self.get(address) == value
        self.u.hook_add(UC_HOOK_CODE, self.stop, begin=STOP, end=STOP)
        self.u.hook_add(UC_HOOK_CODE, self.reset, begin=0x113f88f0, end=0x113f88f0)
        self.u.hook_add(UC_HOOK_CODE, self.set_sequence, begin=0x11383350, end=0x11383350)
        self.stopped = False
        self.resets, self.setters = [], []

    def put(self, address, value, size=4):
        self.u.mem_write(address, (value & ((1 << (size * 8)) - 1)).to_bytes(size, 'little'))

    def get(self, address, size=4):
        return int.from_bytes(self.u.mem_read(address, size), 'little')

    def state(self):
        return [signed16(self.get(ENTITY + 0x1324, 2)), signed32(self.get(PEV + 0x138)),
            self.get(PEV + 0x140), self.get(PEV + 0x148)]

    def stop(self, u, address, size, unused):
        self.stopped = True
        u.emu_stop()

    def set_sequence(self, u, address, size, unused):
        assert u.reg_read(UC_X86_REG_ECX) == ENTITY
        esp = u.reg_read(UC_X86_REG_ESP)
        self.setters.append([signed32(self.get(esp + 4)), self.get(esp + 8) & 0xff])

    def reset(self, u, address, size, unused):
        assert u.reg_read(UC_X86_REG_ECX) == ENTITY
        esp = u.reg_read(UC_X86_REG_ESP)
        assert self.get(esp) == 0x113833a6
        cache, sequence, frame, rate = self.state()
        self.resets.append([sequence, cache, frame, rate])
        # Expected reset side effect needed by selector; detailed host-model
        # updates are checked through actual SDK code in the C++ cases.
        self.put(PEV + 0x148, bits(1))
        u.reg_write(UC_X86_REG_ESP, esp + 4)
        u.reg_write(UC_X86_REG_EIP, self.get(esp))

    def case(self, row):
        kind, cache, sequence, frame, rate, *args = row
        self.u.mem_write(ENTITY, bytes(0x10000))
        self.u.mem_write(0x21000000, bytes(0x10000))
        for register in (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
            UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_XMM0, UC_X86_REG_XMM1):
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
        self.stopped = False
        self.resets, self.setters = [], []
        if kind == 'S':
            requested, reset = args
            self.put(STACK + 4, requested)
            self.put(STACK + 8, reset)
            self.u.reg_write(UC_X86_REG_ECX, ENTITY)
            entry, stack_delta, limit = 0x11383350, 12, 100
        elif kind == 'L':
            lock, now, x, y, skill, climb, *sequences = args
            assert len(sequences) == 7
            self.put(BEHAVIOR, VTABLE)
            self.put(VTABLE + 0xe4, 0x107b6560)
            self.put(BEHAVIOR + 0x1c, ENTITY)
            self.put(ENTITY + 0x12ec, TYPE)
            self.put(BEHAVIOR + 0x1dd0, lock)
            self.put(BEHAVIOR + 0x2b4, x)
            self.put(BEHAVIOR + 0x2b8, y)
            self.put(BEHAVIOR + 0x1d19, climb, 1)
            self.put(TYPE + 0x1e, skill, 2)
            for offset, value in zip(SEQUENCE_OFFSETS, sequences):
                self.put(TYPE + offset, value, 2)
            self.put(0x122788f4, CLOCK)
            self.put(CLOCK, now)
            self.u.reg_write(UC_X86_REG_ECX, BEHAVIOR)
            entry, stack_delta, limit = 0x113f1af0, 4, 300
        else:
            raise ValueError(kind)
        self.u.emu_start(entry, 0, count=limit)
        assert self.stopped and self.u.reg_read(UC_X86_REG_EIP) == STOP
        assert self.u.reg_read(UC_X86_REG_ESP) == STACK + stack_delta
        assert self.u.reg_read(UC_X86_REG_EBP) == 0
        for register in (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI):
            assert self.u.reg_read(register) == 0
        return [1, *self.state(), len(self.resets), *(value for call in self.resets for value in call)]


def cases():
    result = []
    for cache in [-1, 0, 4, 32767, -32768, 65535]:
        for requested in [-2147483648, -65537, -32769, -32768, -3, -2, -1, 0, 4, 32767, 32768, 65535, 65536, 2147483647]:
            for reset in [0, 1]:
                result.append(('S', cache, 4, bits(91), bits(2), requested, reset))
    slots = [100, 101, 102, 107, 108, 109, 130]
    moves = [(0, 0), (bits(6) - 1, 0), (bits(6), 0), (bits(6) + 1, 0),
        (bits(20) - 1, 0), (bits(20), 0), (bits(20) + 1, 0), (bits(25), 0),
        (bits(12), bits(16)), (bits(20), bits(1)), (bits(-21), bits(0)), (bits(3), bits(4)),
        (1, 0), (0x00800000, 0), (0x80000000, 0), (bits(1e19), 0)]
    for x, y in moves:
        for skill in [-32768, 0, 5, 6, 7, 1002, 32767]:
            for climb in [0, 1]:
                for now in [bits(25) - 1, bits(25), bits(25) + 1]:
                    result.append(('L', 4, 4, bits(91), bits(2), bits(25), now, x, y, skill, climb, *slots))
    for active in [4, 100, 101, 102, 107, 108, 109, 130]:
        for x in [0, bits(10), bits(25)]:
            for skill in [0, 6]:
                for climb in [0, 1]:
                    result.append(('L', active, active, bits(91), bits(7), 0, bits(25), x, 0, skill, climb, *slots))
    for index in range(7):
        for value in [-32768, -3, -2, -1, 0, 32767, 32768, 65535, 65536]:
            changed = slots.copy()
            changed[index] = value
            for x in [0, bits(10), bits(25)]:
                for skill in [0, 6]:
                    for climb in [0, 1]:
                        result.append(('L', 4, 4, bits(91), bits(2), 0, bits(25), x, 0, skill, climb, *changed))
    rng = random.Random(0x113f1af0)
    for _ in range(256):
        result.append(('L', rng.choice([4, 100, 101, 102, 107, 108, 109]), 4,
            bits(rng.uniform(0, 255)), bits(rng.uniform(0, 5)), 0, bits(25),
            bits(rng.uniform(-500, 500)), bits(rng.uniform(-500, 500)), rng.choice([0, 6]), rng.randrange(2), *slots))
    return list(dict.fromkeys(result))


def main():
    parser = argparse.ArgumentParser()
    for name in ['mp', 'metadata', 'exe', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    original = OriginalAnimation(args.mp, args.metadata)
    matrix = cases()
    expected = [original.case(row) for row in matrix]
    commands = [' '.join(map(str, row)) for row in matrix]
    result = subprocess.run([str(args.exe.resolve()), '--oracle'], input='\n'.join(commands) + '\n',
        capture_output=True, text=True, timeout=45)
    actual = [list(map(int, line.split())) for line in result.stdout.splitlines()]
    failures = [{'command': command, 'original': want, 'production': got}
        for command, want, got in zip(commands, expected, actual) if want != got]
    complete = result.returncode == 0 and len(actual) == len(expected)
    evidence = {'scope': 'setter and locomotion only; finite movement without overflow; default MXCSR 0x1f80',
        'cases': len(matrix), 'setterCases': sum(row[0] == 'S' for row in matrix),
        'locomotionCases': sum(row[0] == 'L' for row in matrix), 'complete': complete,
        'failures': failures, 'returnCode': result.returncode, 'stderr': result.stderr,
        'ranges': [{'start': hex(start), 'endExclusive': hex(end),
            'sha256': hashlib.sha256(original.original(start, end - start)).hexdigest()} for start, end in RANGES],
        'instructionLimits': {'setter': 100, 'locomotionIncludingSetterAndGetter': 300},
        'constants': [{'address': hex(address), 'bits': hex(value), 'bytesHex': original.original(address, 4).hex()}
            for address, value in CONSTANTS.items()],
        'stopping': 'explicit STOP hook, EIP, exact post-return ESP, restored EBP/EBX/ESI/EDI',
        'substitutes': ['controlled entity/pev/behavior/type/globals pointers and fields',
            'ResetSequenceInfo captures call state and writes rate=1; C++ separately runs host SDK method body',
            'no engine model lookup, ReAPI plugins, real model loading, AI, movement physics or live gameplay'],
        'originalClimbGetterExecuted': True, 'originalSetterExecuted': True, 'nativeDllLoaded': False,
        'inputs': [support.evidence(path) for path in [args.mp, args.metadata, args.exe, Path(__file__), helper]]}
    args.output.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'cases': len(matrix), 'complete': complete, 'failures': len(failures)}))
    return 0 if complete and not failures else 1


if __name__ == '__main__':
    raise SystemExit(main())
