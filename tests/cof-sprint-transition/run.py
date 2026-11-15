#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute verified original x86 sprint gates against the production profile."""
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
SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'


def identity(path):
    return dict(path=str(path.resolve()), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original', 'ida-evidence', 'compiler', 'environment', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build') or not args.original.resolve().is_relative_to(ROOT):
        raise ValueError('Use a workspace original and fresh build output')
    data = args.original.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA:
        raise ValueError('Original module identity differs')
    pe = struct.unpack_from('<I', data, 60)[0]
    base = struct.unpack_from('<I', data, pe + 52)[0]
    section_table = pe + 24 + struct.unpack_from('<H', data, pe + 20)[0]
    sections = [struct.unpack_from('<IIII', data, section_table + n * 40 + 8)
                for n in range(struct.unpack_from('<H', data, pe + 6)[0])]

    def raw(address, size):
        for _, rva, count, offset in sections:
            if rva <= address - base and address - base + size <= rva + count:
                return data[offset + address - base - rva:offset + address - base - rva + size]
        raise ValueError('Original byte region is outside the PE source')

    evidence = [args.ida_evidence / name for name in
                ['original-raw.json', 'sprint-ida-verified-pages.json', 'verified.json']]
    saved = json.loads(evidence[0].read_text())
    for region in saved if isinstance(saved, list) else saved['result']:
        body = bytes(int(word, 16) for word in region['data'].split())
        if body != raw(int(region['addr'], 16), len(body)):
            raise ValueError('Current original differs from saved IDA bytes')
    cs = Cs(CS_ARCH_X86, CS_MODE_32)
    cs.detail = True
    pages = json.loads(evidence[1].read_text())['pages']
    recorded = [int(row['addr'], 16) for page in pages for row in page['asm']['lines']]
    decoded = list(cs.disasm(raw(0x100F4D70, 0x100F58D6 - 0x100F4D70), 0x100F4D70))
    if recorded != [i.address for i in decoded] or len(recorded) != 690:
        raise ValueError('Complete original IDA function was not verified')
    start, end = 0x100F4E63, 0x100F539B
    body = raw(start, end - start)
    # cdecl fixture entry preserves nonvolatile registers and supplies ESI/self
    # with the original scratch offsets. The omitted later producer is not run.
    prefix = bytes.fromhex('53 56 57 83 ec 40 8b 74 24 50')
    code = prefix + body + bytes.fromhex('83 c4 40 5f 5e 5b c3')
    ladder_offset = (len(code) + 15) & ~15
    ladder = raw(0x100E4ED0, 16)
    if ladder[-1] != 0xC3:
        raise ValueError('Original ladder helper boundary changed')
    code += bytes([0xCC]) * (ladder_offset - len(code)) + ladder
    constants = {0x101A1390: 0, 0x101A9724: 1, 0x102268B0: 2,
                 0x102268B4: 3, 0x102268B8: 4, 0x102268BC: 5}
    if raw(0x101A1390, 4) != struct.pack('<f', 1) or raw(0x101A9724, 4) != struct.pack('<f', 5):
        raise ValueError('Original entry constants changed')
    fixups = []
    for instruction in cs.disasm(body, start):
        offset = len(prefix) + instruction.address - start
        for operand in instruction.operands:
            if operand.type == X86_OP_MEM and not operand.mem.base and not operand.mem.index:
                if operand.mem.disp not in constants or instruction.disp_size != 4:
                    raise ValueError('Unbound original absolute operand: ' + hex(instruction.address))
                fixups.append((offset + instruction.disp_offset, 0, constants[operand.mem.disp]))
        if instruction.mnemonic == 'call':
            target = instruction.operands[0]
            if target.type != X86_OP_IMM or instruction.imm_size != 4:
                raise ValueError('Unbound original call')
            if target.imm == 0x100E4ED0:
                kind = 1
            elif target.imm in [0x100F6AA0, 0x1016BE90]:
                kind = 2  # Explicit failure sink, never simulated success.
            else:
                raise ValueError('Unexpected original call: ' + hex(target.imm))
            fixups.append((offset + instruction.imm_offset, kind, 0))
        elif instruction.mnemonic.startswith('j'):
            target = instruction.operands[0]
            if target.type != X86_OP_IMM or not start <= target.imm <= end:
                raise ValueError('Original branch escapes the declared gate window')
    header = '// Private generated original oracle bytes; do not publish.\n'
    header += 'inline constexpr unsigned char OriginalSprint[]{' + ','.join(hex(b) for b in code) + '};\n'
    header += 'inline constexpr unsigned LadderOffset=' + str(ladder_offset) + ';\n'
    header += 'struct OriginalFixup { unsigned offset, kind, target; };\n'
    header += 'inline constexpr OriginalFixup OriginalFixups[]{' + ','.join(
        '{' + ','.join(map(str, fix)) + '}' for fix in fixups) + '};\n'
    source = Path(__file__).with_name('main.cpp')
    production = ROOT / 'native/csol/scenario/custom/stamina.cpp'
    paths = [Path(__file__), source, production, production.with_suffix('.hpp'),
             production.with_name('phase.hpp'), args.original, *evidence, args.compiler, args.environment]
    before = [identity(p) for p in paths]
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    out.mkdir(parents=True)
    (out / 'original_sprint.inc').write_text(header, encoding='utf-8')
    temporary = out / 'temporary'
    temporary.mkdir()
    env['TMP'] = env['TEMP'] = str(temporary)
    command = [str(args.compiler.resolve()), '/nologo', '--target=i686-pc-windows-msvc',
        '/std:c++20', '/EHsc', '/MT', '/DNOMINMAX', '/arch:AVX2', '/fp:precise',
        '/clang:-O3', '/clang:-flto=thin', '-fuse-ld=lld', '/I' + str(out), str(source), str(production),
        '/Fe' + str(out / 'oracle.exe'), '/link', '/machine:x86', '/threads:1', '/opt:lldltojobs=1']
    steps = []
    success = False
    try:
        for name, argv in [('compile', command), ('test', [str(out / 'oracle.exe')])]:
            done = subprocess.run(argv, cwd=out, env=env, capture_output=True, timeout=120)
            text = (done.stdout + done.stderr).decode('utf-8', 'replace')
            (out / (name + '.log')).write_text(text, encoding='utf-8')
            steps.append(dict(name=name, command=argv, exit=done.returncode))
            print(name, done.returncode, text[-3500:], flush=True)
            if done.returncode:
                raise RuntimeError(name)
        success = True
    finally:
        stable = before == [identity(p) for p in paths]
        (out / 'receipt.json').write_text(json.dumps(dict(success=success and stable,
            inputsUnchanged=stable, inputs=before, steps=steps, sourceCopies=0, deployed=False,
            gameplay=False, performance=False, originalWindow=dict(start=hex(start), endExclusive=hex(end)),
            boundary='Actual original early/entry/stop x86 instructions with original ladder helper; controlled owner fields/globals, no grounded-stairs helper or sqrt error path, no timer/audio/FOV/PM/gameplay evidence'), indent=2) + '\n')
    if not success or not stable:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
