"""Bounded original MP reset-row instructions vs production RestartRowAt only."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_EIP, UC_X86_REG_MXCSR

START, STOP = 0x113A6070, 0x113A60B0
MEMORY = 0x20000000
ROW = MEMORY + 0x100
OFFSET = MEMORY + 0x300
FRAME = MEMORY + 0x500


def evidence(path):
    data = path.read_bytes()
    return {'path': str(path.resolve()), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mp', type=Path, required=True)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    raw = args.mp.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == '39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2'
    meta = json.loads(args.metadata.read_text(encoding='utf-8-sig'))
    rva = START - int(meta['image_base'], 16)
    section = next(item for item in meta['sections'] if item['rva'] <= rva and rva + STOP - START <= item['rva'] + item['raw_size'])
    offset = section['raw_offset'] + rva - section['rva']
    code = raw[offset:offset + STOP - START]
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(START & ~0xfff, 4096)
    u.mem_write(START, code)
    u.mem_map(MEMORY, 4096)
    cases = []
    for start, end in [(0, 0), (3, 20), (-3, -7), (16777217, 16777219), (-16777217, -16777219), (2147483520, 0), (-2147483648, 0)]:
        for elapsed in [0., 0.75, -0.75, 1.25, -1.25, 128.5]:
            cases.append((start, end, 3, bits(elapsed)))
    cases += [(2, 0, 0, bits(10.75)), (2, -1, 32767, bits(10.75)), (-2, 0, 1, bits(1.)),
              (2147483647, 0, 1, bits(0.)), (2147483647, 0, 1, bits(-128.)),
              (0, 2147483647, 1, bits(0.)), (0, 0, -1, bits(0.)),
              (0, 0, 1, bits(math.inf)), (0, 0, 1, bits(-math.inf)), (0, 0, 1, bits(math.nan))]
    expected = []
    for start, end, interval, elapsed_bits in cases:
        u.mem_write(MEMORY, bytes(4096))
        u.mem_write(ROW, struct.pack('<iih', -1, 123, 27))
        u.mem_write(ROW + 0x4c, struct.pack('<iih', start, end, interval))
        u.mem_write(OFFSET, struct.pack('<I', elapsed_bits))
        for register, value in [(UC_X86_REG_EBX, OFFSET), (UC_X86_REG_ESI, 0), (UC_X86_REG_EDI, ROW), (UC_X86_REG_EBP, FRAME), (UC_X86_REG_MXCSR, 0x1f80)]:
            u.reg_write(register, value)
        u.emu_start(START, STOP, count=100)
        assert u.reg_read(UC_X86_REG_EIP) == STOP
        result = list(struct.unpack('<iih', u.mem_read(ROW, 10)))
        # Unicorn does not reliably set cvttss2si's invalid flag. The original
        # integer output still comes from its executed instructions; determine
        # our explicit rejection policy independently from binary32 input math.
        invalid_conversion = bool(u.reg_read(UC_X86_REG_MXCSR) & 1)
        elapsed = struct.unpack('<f', struct.pack('<I', elapsed_bits))[0]
        def single(value):
            try: return struct.unpack('<f', struct.pack('<f', value))[0]
            except OverflowError: return math.copysign(math.inf, value)
        shifted = [single(single(time) + elapsed) for time in (start, end) if time != 0 or time == start]
        valid = interval >= 0 and math.isfinite(elapsed) and all(math.isfinite(value) and -2147483648 <= value < 2147483648 for value in shifted)
        expected.append({'input': [start, end, interval, elapsed_bits], 'originalOutput': result,
                         'originalInvalidConversion': invalid_conversion,
                         'independentShiftedForSafetyPolicy': [str(value) for value in shifted],
                         'expectedProduction': [1, *result] if valid else [0, 111, 222, 33]})
    commands = ''.join(' '.join(map(str, row['input'])) + '\n' for row in expected)
    run = subprocess.run([str(args.exe), '--oracle'], input=commands.encode('ascii'), capture_output=True, check=False)
    assert run.returncode == 0, run.stderr
    lines = [list(map(int, line.split())) for line in run.stdout.decode('ascii').splitlines()]
    errors = []
    if len(lines) != len(expected): errors.append('output line count mismatch')
    for index, (actual, row) in enumerate(zip(lines, expected)):
        row['actualProduction'] = actual
        if actual != row['expectedProduction']: errors.append(f'case {index}: {actual} != {row["expectedProduction"]}')
    result = {'inputs': [evidence(args.mp), evidence(args.metadata), evidence(args.exe), evidence(Path(__file__))],
              'span': {'start': hex(START), 'endExclusive': hex(STOP), 'bytes': len(code), 'sha256': hashlib.sha256(code).hexdigest()},
              'cases': expected, 'checks': len(expected), 'failures': len(errors), 'errors': errors,
              'substitutions': ['synthetic original row/frame/offset memory', 'MXCSR fixed 0x1f80'],
              'exclusions': ['no original full wave reset loop, active-list insertion or observers', 'no original manager, native DLL load or live gameplay'],
              'policy': 'Original invalid cvttss2si and negative interval results rejected transactionally by production.'}
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f'wave_activation_original checks={len(expected)} failures={len(errors)}')
    if errors: print('\n'.join(errors))
    return bool(errors)


if __name__ == '__main__':
    sys.exit(main())
