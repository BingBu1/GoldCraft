#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the complete verified original x86 input sender against the production edge."""
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
SHA = 'f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd'


def identity(path):
    path = path.resolve(strict=True)
    return dict(path=str(path), bytes=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('original', 'ida-evidence', 'compiler', 'environment', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build') or not args.original.resolve().is_relative_to(ROOT):
        raise ValueError('Use a workspace sample and fresh private build output')
    data = args.original.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA:
        raise ValueError('Wrong original CSOL client identity')
    pe = struct.unpack_from('<I', data, 60)[0]
    if struct.unpack_from('<H', data, pe + 4)[0] != 0x14c or struct.unpack_from('<H', data, pe + 24)[0] != 0x10b:
        raise ValueError('The original input sample must be PE32/x86')
    base = struct.unpack_from('<I', data, pe + 52)[0]
    table = pe + 24 + struct.unpack_from('<H', data, pe + 20)[0]
    sections = [struct.unpack_from('<IIII', data, table + i * 40 + 8)
                for i in range(struct.unpack_from('<H', data, pe + 6)[0])]

    def raw(address, count):
        for _, rva, size, offset in sections:
            if rva <= address - base and address - base + count <= rva + size:
                return data[offset + address - base - rva:offset + address - base - rva + count]
        raise ValueError('Unbacked original operand')

    start, end = 0x02A59180, 0x02A591C5
    code = raw(start, end - start)
    cs = Cs(CS_ARCH_X86, CS_MODE_32)
    cs.detail = True
    decoded = list(cs.disasm(code, start))
    evidence = json.loads(args.ida_evidence.read_text('utf8'))
    saved = next(row['result'] for row in evidence if int(row['scope']['addr'], 16) == start)
    if len(decoded) != 25 or [i.address for i in decoded] != [int(r['addr'], 16) for r in saved['asm']['lines']]:
        raise ValueError('Complete original IDA instruction boundaries differ')
    if raw(0x0319B0B4, 9) != b'sprintON\0' or raw(0x0319B0C0, 10) != b'sprintOFF\0':
        raise ValueError('Original command identities differ')
    fixups = []
    for ins in decoded:
        operand = ins.operands[0] if ins.operands else None
        if ins.mnemonic == 'call':
            if operand.type != X86_OP_MEM or operand.mem.base or operand.mem.index or operand.mem.disp != 0x03AE8DFC or ins.disp_size != 4:
                raise ValueError('Unexpected original command callback')
            fixups.append((ins.address - start + ins.disp_offset, 0))
        elif ins.mnemonic == 'push' and operand.type == X86_OP_IMM:
            if operand.imm not in (0x0319B0B4, 0x0319B0C0) or ins.imm_size != 4:
                raise ValueError('Unexpected original immediate')
            fixups.append((ins.address - start + ins.imm_offset, 1 if operand.imm == 0x0319B0B4 else 2))
        elif ins.mnemonic.startswith('j') and not start <= operand.imm < end:
            raise ValueError('Original branch escapes complete input body')
    if len(fixups) != 4:
        raise ValueError('Incomplete original command operands')
    source = Path(__file__).with_name('oracle.cpp')
    paths = [Path(__file__), source, ROOT / 'native/csol/scenario/custom/standalone_input.hpp',
             args.original, args.ida_evidence, args.compiler, args.environment]
    before = [identity(p) for p in paths]
    out.mkdir(parents=True)
    header = 'struct InputFixup { unsigned offset,binding; };\n'
    header += 'inline constexpr unsigned char OriginalInput[]{' + ','.join(hex(x) for x in code) + '};\n'
    header += 'inline constexpr InputFixup OriginalInputFixups[]{' + ','.join('{' + f'{a},{b}' + '}' for a, b in fixups) + '};\n'
    (out / 'original_input.inc').write_text(header, 'utf8')
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    temporary = out / 'temporary'
    temporary.mkdir()
    env['TMP'] = env['TEMP'] = str(temporary)
    command = [str(args.compiler.resolve()), '/nologo', '--target=i686-pc-windows-msvc', '/std:c++20',
               '/MT', '/EHsc', '/DNOMINMAX', '/arch:AVX2', '/fp:precise', '/utf-8', '/clang:-O3',
               '/clang:-flto=thin', '-fuse-ld=lld', '/I' + str(out), str(source), '/Fe' + str(out / 'oracle.exe'),
               '/link', '/machine:x86', '/threads:1', '/opt:lldltojobs=1']
    success, steps = False, []
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
            inputs=before, originalWindow=dict(start=hex(start), endExclusive=hex(end), instructions=25,
            bytes=len(code), sha256=hashlib.sha256(code).hexdigest()), steps=steps, sourceCopies=0,
            deployed=False, gameplay=False, performance=False,
            boundary='Complete original x86 sender with explicit command-buffer callback boundary. No whole-client/gameplay evidence.'), indent=2), 'utf8')
    if not success or not stable:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
