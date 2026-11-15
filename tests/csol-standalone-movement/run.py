#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the original x86 displacement/speed windows against the production core."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

ROOT = Path(__file__).resolve().parents[2]
SHA = '39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2'


def identity(path):
    path = path.resolve(strict=True)
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original', 'ida-evidence', 'compiler', 'environment', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build') or not args.original.resolve().is_relative_to(ROOT):
        raise ValueError('Use the workspace original and fresh build output')
    data = args.original.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA:
        raise ValueError('Original CSOL identity differs')
    pe = struct.unpack_from('<I', data, 60)[0]
    base = struct.unpack_from('<I', data, pe + 52)[0]
    size_image = struct.unpack_from('<I', data, pe + 80)[0]
    table = pe + 24 + struct.unpack_from('<H', data, pe + 20)[0]
    sections = [struct.unpack_from('<IIII', data, table + n * 40 + 8)
                for n in range(struct.unpack_from('<H', data, pe + 6)[0])]

    def raw(address, count):
        for _, rva, size, offset in sections:
            if rva <= address - base and address - base + count <= rva + size:
                return data[offset + address - base - rva:offset + address - base - rva + count]
        raise ValueError('Unbacked original address: ' + hex(address))

    evidence_files = [args.ida_evidence / name for name in ('movement-ida.json', 'continuation-run-ida.json')]
    saved = {int(row['addr'], 16): row for file in evidence_files
             for row in json.loads(file.read_text('utf8'))}
    for address, value in ((0x117AD578, .8), (0x1178F588, .5), (0x11793134, .2)):
        if raw(address, 4) != struct.pack('<f', value):
            raise ValueError('Original speed/noise constant differs')
    if raw(0x100CF8C3, 7) != bytes.fromhex('c7 45 fc') + struct.pack('<f', .1):
        raise ValueError('Original distance debit initializer differs')
    cs = Cs(CS_ARCH_X86, CS_MODE_32)
    cs.detail = True
    # All original globals are bound explicitly. Noise calls are fatal in the
    # fixture; its deadline equals now, excluding that separate service path.
    globals_ = {0x11EE502C: 1, 0x11EE5048: 2, 0x11EE5030: 3, 0x122788F4: 4,
                0x11EE5008: 5, 0x11EE4DA0: 5, 0x122786D0: 6, 0x12278A14: 5,
                0x11793134: 7, 0x12278698: 9, 0x117AD578: 10, 0x1178F588: 11}
    calls = {0x115306A0: 1, 0x115907CA: 2, 0x115B39EF: 3, 0x115B3977: 3,
             0x106DB880: 3, 0x10609130: 3, 0x114BE110: 3, 0x11592754: 3}
    windows, header = [], '// Private original instruction oracle; never publish.\n'
    header += 'struct Fixup { unsigned offset,kind,target; };\n'
    for label, start, end, count in [('Run', 0x108595D0, 0x10859801, 140),
                                     ('Speed', 0x108588E0, 0x10858978, 45)]:
        code = raw(start, end - start)
        decoded = list(cs.disasm(code, start))
        rows = saved[start]['asm']['lines']
        if len(decoded) != count or [i.address for i in decoded] != [int(r['addr'], 16) for r in rows]:
            raise ValueError('Complete IDA instruction boundaries differ: ' + label)
        if label == 'Run':
            cookie = next(i for i in decoded if i.address == 0x108595DC)
            globals_[cookie.operands[1].mem.disp] = 0
        fixups = []
        for instruction in decoded:
            offset = instruction.address - start
            for operand in instruction.operands:
                if operand.type == X86_OP_MEM and not operand.mem.base and not operand.mem.index:
                    if operand.mem.disp not in globals_ or instruction.disp_size != 4:
                        raise ValueError('Unbound original global: ' + hex(instruction.address))
                    fixups.append((offset + instruction.disp_offset, 0, globals_[operand.mem.disp]))
            operand = instruction.operands[0] if instruction.operands else None
            if instruction.mnemonic == 'call' and operand.type == X86_OP_IMM:
                if operand.imm not in calls or instruction.imm_size != 4:
                    raise ValueError('Unbound original call: ' + hex(operand.imm))
                fixups.append((offset + instruction.imm_offset, calls[operand.imm], 0))
            elif instruction.mnemonic == 'push' and operand.type == X86_OP_IMM:
                value = operand.imm
                if value in globals_:
                    fixups.append((offset + instruction.imm_offset, 0, globals_[value]))
                elif base <= value < base + size_image:
                    text = raw(value, 64).split(b'\0')[0]
                    kind = 12 if text == b'sv_maxspeed' else 8 if text == b'makenoise %d %.2f %.2f %.2f %.2f\n' else None
                    if kind is None or instruction.imm_size != 4:
                        raise ValueError('Unbound original pushed address: ' + hex(value))
                    fixups.append((offset + instruction.imm_offset, 0, kind))
            elif instruction.mnemonic.startswith('j') and not start <= operand.imm < end:
                raise ValueError('Original branch escapes window')
        header += 'inline constexpr unsigned char Original' + label + '[]{' + ','.join(hex(b) for b in code) + '};\n'
        header += 'inline constexpr Fixup Original' + label + 'Fixups[]{' + ','.join(
            '{' + ','.join(map(str, row)) + '}' for row in fixups) + '};\n'
        windows.append(dict(name=label, start=hex(start), endExclusive=hex(end), instructions=count,
                            bytes=len(code), sha256=hashlib.sha256(code).hexdigest()))
    source = Path(__file__).with_name('main.cpp')
    paths = [Path(__file__), source, ROOT / 'native/csol/scenario/custom/standalone_movement.hpp',
             args.original, *evidence_files, args.compiler, args.environment]
    before = [identity(p) for p in paths]
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for previous in list(env):
            if previous.casefold() == key.casefold():
                del env[previous]
        env[key] = value
    out.mkdir(parents=True)
    (out / 'original_movement.inc').write_text(header, 'utf8')
    temporary = out / 'temporary'
    temporary.mkdir()
    env['TMP'] = env['TEMP'] = str(temporary)
    command = [str(args.compiler.resolve()), '/nologo', '--target=i686-pc-windows-msvc',
               '/std:c++20', '/EHsc', '/MT', '/DNOMINMAX', '/arch:AVX2', '/fp:precise', '/utf-8',
               '/clang:-O3', '/clang:-flto=thin', '-fuse-ld=lld', '/I' + str(out), str(source),
               '/Fe' + str(out / 'oracle.exe'), '/link', '/machine:x86', '/threads:1', '/opt:lldltojobs=1']
    steps, success = [], False
    try:
        for name, argv in [('compile', command), ('test', [str(out / 'oracle.exe')])]:
            result = subprocess.run(argv, cwd=out, env=env, capture_output=True, timeout=120)
            text = (result.stdout + result.stderr).decode('utf8', 'replace')
            (out / (name + '.log')).write_text(text, 'utf8')
            steps.append(dict(name=name, command=argv, exit=result.returncode))
            print(name, result.returncode, text[-4000:], flush=True)
            if result.returncode:
                raise RuntimeError(name)
        success = True
    finally:
        stable = before == [identity(p) for p in paths]
        (out / 'receipt.json').write_text(json.dumps(dict(success=success and stable, inputsUnchanged=stable,
            inputs=before, windows=windows, steps=steps, sourceCopies=0, deployed=False, gameplay=False,
            performance=False, boundary='Original displacement/speed arithmetic with explicit player/ResetMaxSpeed/CVAR boundaries; noise deadline prevents the separate makenoise service. No active input/PM/prediction/whole-mode acceptance.'), indent=2), 'utf8')
    if not success or not stable:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
