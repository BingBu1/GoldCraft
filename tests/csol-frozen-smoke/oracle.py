"""Compare production Tick with the original client DLL's actual x86 Update.

The sample is user-provided local evidence and is never published. Unicorn
executes only the listed original particle functions. Engine time and camera
imports are controlled stubs; the complete derived/base update runs unmodified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

CLIENT_SHA = 'f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd'
PARTICLE, STACK, STOP = 0x20000000, 0x20018000, 0x20020000
TIME_STUB, ANGLE_STUB, CLOCK = 0x20021000, 0x20021010, 0x20022000
FUNCTIONS = [
    (0x2ad3f10, 0x2ad42dc), (0x2ad3e50, 0x2ad3f10), (0x1de5260, 0x1de52f5),
    (0x1de10f0, 0x1de1138), (0x1de2af0, 0x1de2b45), (0x1de3430, 0x1de3481),
    (0x1de3490, 0x1de3513), (0x1de5130, 0x1de5254), (0x2ad5000, 0x2ad50c0),
    (0x1de2070, 0x1de2204), (0x1de2210, 0x1de2228), (0x1de2752, 0x1de275a),
]
f32 = lambda value: struct.unpack('<f', struct.pack('<f', value))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--client-dll', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(args.output)
    path = args.client_dll or args.source_root / 'analysis/csol-model-20261017/samples/client.dll'
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != CLIENT_SHA:
        raise ValueError('unrecognized client sample: original instructions are build-specific')
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    if raw[pe:pe + 4] != b'PE\0\0' or struct.unpack_from('<H', raw, pe + 4)[0] != 0x14c:
        raise ValueError('expected x86 PE')
    count, optional_size = struct.unpack_from('<H', raw, pe + 6)[0], struct.unpack_from('<H', raw, pe + 20)[0]
    base, image_size = struct.unpack_from('<I', raw, pe + 52)[0], struct.unpack_from('<I', raw, pe + 80)[0]
    sections = []
    for index in range(count):
        at = pe + 24 + optional_size + index * 40
        virtual_size, rva, size, offset = struct.unpack_from('<IIII', raw, at + 8)
        sections.append((rva, size, offset))
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(base, image_size)
    for rva, size, offset in sections:
        u.mem_write(base + rva, raw[offset:offset + size])
    u.mem_map(PARTICLE, 0x30000)
    u.mem_write(TIME_STUB, b'\xdd\x05' + struct.pack('<I', CLOCK) + b'\xc3')
    u.mem_write(ANGLE_STUB, b'\xc3')
    u.mem_write(0x3ae8e84, struct.pack('<I', TIME_STUB))
    u.mem_write(0x3ae8e0c, struct.pack('<I', ANGLE_STUB))
    visited = set()

    def guard(_u, address, _size, _data):
        if address == 0x2ad39f0:
            esp = u.reg_read(UC_X86_REG_ESP)
            ret, destination = struct.unpack('<II', u.mem_read(esp, 8))
            u.mem_write(destination, struct.pack('<fff', 27, -49, 0))
            u.reg_write(UC_X86_REG_EAX, destination)
            u.reg_write(UC_X86_REG_ESP, esp + 4)
            u.reg_write(UC_X86_REG_EIP, ret)
            return
        if TIME_STUB <= address < TIME_STUB + 7 or address == ANGLE_STUB:
            return
        if not any(begin <= address < end for begin, end in FUNCTIONS):
            raise AssertionError(f'unexpected original execution at {address:#x}')
        visited.add(address)

    u.hook_add(UC_HOOK_CODE, guard)

    def write(offset, value):
        u.mem_write(PARTICLE + offset, struct.pack('<f', value))

    def read(offset):
        return struct.unpack('<f', u.mem_read(PARTICLE + offset, 4))[0]

    rows, inputs = [], []
    for born in (1.0, 100.0, 4096.0):
        for size, growth in ((220.0, 3.0), (227.5, 3.5), (250.0, 5.0)):
            for gravity in (0.0, 400.0, 800.0, 1000.0):
                for age in (0, .0001, .016, .1, .5, 1, 2, 3, 3.9, 3.96, 3.99, 4, 5, 1200):
                    now = f32(born + age)
                    previous = f32(max(born, now - .03125))
                    alpha = f32(110 * max(0, 1 - (previous - born) / 4))
                    initial = [born, f32(born + 4), size, growth, alpha, 16384.25, -9000.125, 64,
                               1.25, -2.5, 51.25, now, previous, gravity]
                    inputs.append(' '.join(format(value, '.9g') for value in initial) + '\n')
                    u.mem_write(PARTICLE, bytes(0x200))
                    u.mem_write(PARTICLE, struct.pack('<I', 0x31a6224))
                    for offset, value in (
                        (0x50, size), (0x54, growth), (0x64, alpha), (0x68, 0),
                        (0x6c, born), (0x70, f32(born + 4)), (0x74, .05),
                        (0x94, 16384.25), (0x98, -9000.125), (0x9c, 64),
                        (0xa8, 1), (0xb4, .1), (0xb8, 1.25), (0xbc, -2.5), (0xc0, 51.25),
                        (0x128, size), (0x134, 1), (0x138, 110),
                    ):
                        write(offset, value)
                    u.mem_write(0x3634978, struct.pack('<f', previous))
                    u.mem_write(0x3c4d760, struct.pack('<f', gravity))
                    u.mem_write(CLOCK, struct.pack('<d', now))
                    u.reg_write(UC_X86_REG_ECX, PARTICLE)
                    u.reg_write(UC_X86_REG_ESP, STACK)
                    u.mem_write(STACK, struct.pack('<If', STOP, now))
                    u.emu_start(0x2ad3f10, STOP, count=5000)
                    if u.reg_read(UC_X86_REG_EIP) != STOP:
                        raise AssertionError('instruction budget exhausted')
                    die = read(0x70)
                    expected = [int(die == 0 or now < die), read(0x50), read(0x64), read(0xa8), die,
                                read(0x94), read(0x98), read(0x9c), read(0xb8), read(0xbc), read(0xc0)]
                    if read(0xa0) != 27 or read(0xa4) != -49 or int.from_bytes(u.mem_read(PARTICLE + 0x8c, 4), 'little') != 0:
                        raise AssertionError('original camera or fixed-frame contract changed')
                    rows.append(dict(input=initial, original=expected))
    result = subprocess.run([str(args.executable.resolve()), '--oracle'], input=''.join(inputs),
                            text=True, capture_output=True, timeout=60, check=True)
    actual = [json.loads(line) for line in result.stdout.splitlines()]
    if len(actual) != len(rows):
        raise AssertionError((len(actual), len(rows)))
    failures = []
    for row, values in zip(rows, actual):
        row['production'] = values
        if any(f32(expected) != f32(value) for expected, value in zip(row['original'], values)):
            failures.append(row)
    windows = []
    for begin, end in FUNCTIONS:
        windows.append(dict(begin=hex(begin), end=hex(end),
                            sha256=hashlib.sha256(bytes(u.mem_read(begin, end - begin))).hexdigest()))
    report = dict(clientSha256=CLIENT_SHA, executableSha256=hashlib.sha256(args.executable.read_bytes()).hexdigest(),
                  samples=len(rows), failureCount=len(failures), failures=failures,
                  functions=windows, executedInstructionAddresses=len(visited), rows=rows,
                  scope='unmodified original derived/base particle update; controlled engine time/camera imports')
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'frozen smoke original DLL: {len(rows)} samples, {len(failures)} failures')
    if failures:
        raise AssertionError(failures[:3])


if __name__ == '__main__':
    main()
