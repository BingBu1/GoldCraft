"""Three bounded original arithmetic stages, not the full monster damage path.

The DLL is parsed as data, never loaded. Controlled entity/frame inputs replace
the caller. Ordinary EHANDLE resolution is the only substituted call. A synthetic
FNINIT/FLD prepares the armor stage's x87 input before original code starts.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import math
import random
import struct
import subprocess
import sys
sys.dont_write_bytecode = True
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP,
    UC_X86_REG_EFLAGS, UC_X86_REG_MXCSR, UC_X86_REG_XMM0, UC_X86_REG_XMM1,
    UC_X86_REG_XMM2, UC_X86_REG_XMM3, UC_X86_REG_XMM4, UC_X86_REG_XMM5, UC_X86_REG_XMM6)

helper = Path(__file__).parents[1] / 'csol-scenario/spawn_points_original.py'
spec = importlib.util.spec_from_file_location('damage_binary_support', helper)
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)
ENTITY, PEV, TRACE, PLAYER, WEAPON, HANDLE, INPUT = [0x20000000 + i * 0x2000 for i in range(7)]
STACK, FRAME, ARGS = 0x21008000, 0x21009000, 0x2100b000
SETUP, STOP = 0x30000000, 0x30001000
SENTINEL, ARMOR_SENTINEL = 0x4f123456, 0x4e654321
RANGES = {'T': (0x1142a098, 0x1142a0e8, 40),
    'O': (0x113ee690, STOP, 100), 'A': (0x113863d9, 0x11386484, 90)}
CONSTANTS = {0x11790290: ('f', 225.0), 0x117f18c8: ('f', 48.0),
    0x1180d46c: ('f', 330.0), 0x1180d470: ('f', 345.0),
    0x11792ae0: ('f', 3.0), 0x117cfe18: ('d', 1.25),
    0x1181b250: ('d', 0.75), 0x117e7388: ('d', 0.4),
    0x117ce0e0: ('d', 1.5), 0x1178f588: ('f', 0.5), 0x1178f58c: ('f', 1.0)}


def bits(value):
    return int.from_bytes(struct.pack('<f', value), 'little')


def finite(value):
    return value & 0x7f800000 != 0x7f800000


class OriginalDamage:
    original = support.Original.original

    def __init__(self, path, metadata):
        self.data = path.read_bytes()
        self.meta = json.loads(metadata.read_text(encoding='utf-8-sig'))
        assert hashlib.sha256(self.data).hexdigest() == support.EXPECTED_MP == self.meta['sha256']
        assert int(self.meta['image_base'], 16) == 0x10000000
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        pages = {0x113ee000, 0x1142a000, 0x11386000, 0x1061f000}
        pages.update(address & ~0xfff for address in CONSTANTS)
        for page in sorted(pages):
            self.u.mem_map(page, 0x1000)
            self.u.mem_write(page, self.original(page, 0x1000))
        self.u.mem_map(ENTITY, 0x10000)
        self.u.mem_map(0x21000000, 0x10000)
        self.u.mem_map(SETUP, 0x2000)
        self.u.mem_write(SETUP, b'\xdb\xe3\xd9\x05' + struct.pack('<I', INPUT))
        for address, (kind, value) in CONSTANTS.items():
            assert self.u.mem_read(address, struct.calcsize(kind)) == struct.pack('<' + kind, value)
        for _, end, _ in RANGES.values():
            self.u.hook_add(UC_HOOK_CODE, self.stop, begin=end, end=end)
        self.u.hook_add(UC_HOOK_CODE, self.handle, begin=0x1061f2e0, end=0x1061f2e0)
        self.stop_at = self.stopped_at = None
        self.handle_calls = 0

    def put(self, address, value):
        self.u.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def get(self, address):
        return int.from_bytes(self.u.mem_read(address, 4), 'little')

    def stop(self, u, address, size, unused):
        if address == self.stop_at:
            self.stopped_at = address
            u.emu_stop()

    def handle(self, u, address, size, unused):
        assert u.reg_read(UC_X86_REG_ECX) == PLAYER + 0x1034
        esp = u.reg_read(UC_X86_REG_ESP)
        assert self.get(esp) == 0x113ee6cf
        self.handle_calls += 1
        u.reg_write(UC_X86_REG_EAX, WEAPON)
        u.reg_write(UC_X86_REG_ESP, esp + 4)
        u.reg_write(UC_X86_REG_EIP, self.get(esp))

    def setup(self, input_bits, armor=False):
        self.u.mem_write(ENTITY, bytes(0x10000))
        self.u.mem_write(0x21000000, bytes(0x10000))
        for register in (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
            UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP,
            UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3,
            UC_X86_REG_XMM4, UC_X86_REG_XMM5, UC_X86_REG_XMM6):
            self.u.reg_write(register, 0)
        self.u.reg_write(UC_X86_REG_EFLAGS, 2)
        self.u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        self.u.reg_write(UC_X86_REG_ESP, STACK)
        self.put(INPUT, input_bits)
        setup_end = SETUP + (8 if armor else 2)
        self.u.emu_start(SETUP, setup_end, count=4)
        assert self.u.reg_read(UC_X86_REG_EIP) == setup_end
        assert self.u.reg_read(UC_X86_REG_ESP) == STACK
        self.handle_calls = 0

    def case(self, row, context='valid'):
        kind, input_bits, *rest = row
        self.setup(input_bits, kind == 'A')
        if kind == 'T':
            self.u.reg_write(UC_X86_REG_EBP, FRAME)
            self.u.reg_write(UC_X86_REG_ESI, TRACE)
            self.put(FRAME + 0xc, input_bits)
            self.put(TRACE + 0x34, rest[0])
        elif kind == 'O':
            hit, weapon = rest
            self.u.mem_write(STACK, struct.pack('<IIIII', STOP,
                PLAYER if context != 'indirect' else ENTITY,
                PLAYER if context != 'no-player' else 0, input_bits, hit & 0xffffffff))
            self.put(PLAYER + 0x1038, HANDLE if context != 'no-active' else 0)
            self.put(HANDLE + 4, 1 if context != 'empty-active' else 0)
            self.put(WEAPON + 0xe4, weapon)
        elif kind == 'A':
            armor_bits, flags = rest
            self.u.reg_write(UC_X86_REG_EBP, FRAME)
            self.u.reg_write(UC_X86_REG_EBX, ARGS)
            self.u.reg_write(UC_X86_REG_ESI, ENTITY)
            self.put(ENTITY + 8, PEV)
            self.put(PEV + 0x1e8, armor_bits)
            self.put(ARGS + 0x14, flags)
        else:
            raise ValueError(kind)
        start, end, limit = RANGES[kind]
        self.stop_at, self.stopped_at = end, None
        self.u.emu_start(start, 0, count=limit)
        assert self.stopped_at == end and self.u.reg_read(UC_X86_REG_EIP) == end, (row, 'stop boundary')
        assert self.u.reg_read(UC_X86_REG_ESP) == STACK + (20 if kind == 'O' else 0), (row, 'stack boundary')
        if kind == 'T':
            assert self.handle_calls == 0
            return [self.u.reg_read(UC_X86_REG_XMM5) & 0xffffffff]
        if kind == 'O':
            assert self.u.reg_read(UC_X86_REG_EBP) == 0
            assert self.handle_calls == int(context == 'valid')
            return [self.get(STACK + 12)]
        assert self.handle_calls == 0
        return [self.u.reg_read(UC_X86_REG_XMM1) & 0xffffffff, self.get(PEV + 0x1e8)]


def cases():
    result = []
    values = [0xff7fffff, bits(-345), bits(-1), 0x80800000, 0x80000001, 0x80000000,
        0, 1, 2, 0x007fffff, 0x00800000, bits(0.1), bits(1), bits(10),
        bits(48), bits(225), bits(330), bits(345), bits(500), 0x4b800001, 0x7f7fffff]
    hitgroups = [-2147483648, -1, *range(11), 2147483647]
    result.extend(['T', value, hit] for value in values for hit in hitgroups)
    # Exercise every small ID and all higher minimum IDs, then every hitgroup.
    result.extend(['O', bits(1), 0, weapon] for weapon in [-2147483648, -1, *range(513), 2147483647])
    for weapon in [0, 3, 18, 40, 66, 53, 71, 311, 415, 447]:
        result.extend(['O', value, hit, weapon] for value in values for hit in hitgroups)
    # Values immediately around floor thresholds exercise MAXSS and rounding.
    for weapon, floor in [(3, 225), (18, 345), (40, 48), (66, 330)]:
        result.extend(['O', bits(floor) + offset, hit, weapon]
            for offset in [-1, 0, 1] for hit in [0, 1, 3, 6, 8, 9])
    armor_values = [bits(-100), bits(-10), 0x80000000, 0, 1,
        bits(1), bits(10), bits(25), bits(40), bits(50), 0x7f7fffff]
    result.extend(['A', value, armor, flags] for value in values for armor in armor_values
        for flags in [0, 0x40, 0x01000000, 0x20, 0xffffffff])
    result.extend(['A', bits(100), bits(40), 1 << bit] for bit in range(32))
    rng = random.Random(0x113ee690)
    for _ in range(128):
        result.append(['T', bits(rng.uniform(-10000, 10000)), rng.choice(hitgroups)])
        result.append(['O', bits(rng.uniform(-10000, 10000)), rng.choice(hitgroups), rng.choice([0, 3, 40, 66, 447])])
        result.append(['A', bits(rng.uniform(-10000, 10000)), bits(rng.uniform(-100, 1000)), rng.getrandbits(32)])
    return list(dict.fromkeys(tuple(row) for row in result))


def main():
    parser = argparse.ArgumentParser()
    for name in ['mp', 'metadata', 'exe', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    original = OriginalDamage(args.mp, args.metadata)
    matrix = cases()
    raw = [original.case(row) for row in matrix]
    # These four original context branches correspond to the caller supplying 0.
    contexts = [(context, ['O', bits(1), hit, 3])
        for context in ['no-player', 'indirect', 'no-active', 'empty-active'] for hit in [0, 1, 3, 8, 9]]
    for context, row in contexts:
        raw.append(original.case(row, context))
        matrix.append(tuple([*row[:3], 0]))
    expected = [[1, *values] if all(finite(value) for value in values)
        else [0, SENTINEL, ARMOR_SENTINEL][:len(values) + 1] for values in raw]
    commands = [' '.join(map(str, row)) for row in matrix]
    result = subprocess.run([str(args.exe.resolve()), '--oracle'], input='\n'.join(commands) + '\n',
        capture_output=True, text=True, timeout=45)
    actual = [list(map(int, line.split())) for line in result.stdout.splitlines()]
    failures = [{'command': command, 'originalBits': old, 'expectedHostResult': want, 'production': got}
        for command, old, want, got in zip(commands, raw, expected, actual) if want != got]
    complete = result.returncode == 0 and len(actual) == len(expected)
    ranges = [('trace', 0x1142a098, 0x1142a0e8, 40), ('ordinary', 0x113ee690, 0x113ee7f5, 100),
        ('armor', 0x113863d9, 0x11386484, 90)]
    evidence = {'scope': 'three separate scalar stages; all caller inputs finite, default MXCSR 0x1f80',
        'cases': len(matrix), 'stages': {kind: sum(row[0] == kind for row in matrix) for kind in 'TOA'},
        'hostOverflowRejections': sum(want[0] == 0 for want in expected),
        'contextBranches': len(contexts), 'complete': complete, 'failures': failures,
        'returnCode': result.returncode, 'stderr': result.stderr,
        'ranges': [{'name': name, 'start': hex(start), 'endExclusive': hex(end), 'instructionLimit': limit,
            'sha256': hashlib.sha256(original.original(start, end - start)).hexdigest()} for name, start, end, limit in ranges],
        'tables': [{'start': hex(start), 'bytes': size, 'sha256': hashlib.sha256(original.original(start, size)).hexdigest()}
            for start, size in [(0x113ee7f8, 16), (0x113ee808, 49), (0x113ee83c, 36), (0x1142a6ac, 28)]],
        'constants': [{'address': hex(address), 'kind': kind, 'value': value,
            'bytesHex': original.original(address, struct.calcsize(kind)).hex()}
            for address, (kind, value) in CONSTANTS.items()],
        'stopping': 'explicit code hook and EIP for every stage; stack unchanged for T/A, ret16 stack for O',
        'substitutes': ['all trace/entity/pev/frame memory is controlled, not constructed by the engine',
            'ordinary valid active-weapon handle resolver returns controlled weapon and checks ECX/callsite',
            'synthetic FNINIT and armor FLD only prepare input before the original range',
            'mode, entity identity, rules, damage dispatch, modifiers after armor, health and rewards are excluded'],
        'policyDifference': 'finite original outputs match bit for bit; original overflow is rejected by host with output unchanged',
        'inputs': [support.evidence(path) for path in [args.mp, args.metadata, args.exe, Path(__file__), helper]],
        'nativeDllLoaded': False, 'fullTakeDamage': False, 'liveGameplay': False}
    args.output.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'cases': len(matrix), 'complete': complete, 'failures': len(failures)}))
    return 0 if complete and not failures else 1


if __name__ == '__main__':
    raise SystemExit(main())
