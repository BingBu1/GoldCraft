#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare recovery against the actual original CSOL StandAlone x86 function."""
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
        raise ValueError('Use a workspace original and fresh build output')
    data = args.original.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA:
        raise ValueError('Original CSOL identity differs')
    pe = struct.unpack_from('<I', data, 60)[0]
    base = struct.unpack_from('<I', data, pe + 52)[0]
    table = pe + 24 + struct.unpack_from('<H', data, pe + 20)[0]
    sections = [struct.unpack_from('<IIII', data, table + n * 40 + 8)
                for n in range(struct.unpack_from('<H', data, pe + 6)[0])]

    def raw(address, count):
        for _, rva, size, offset in sections:
            if rva <= address - base and address - base + count <= rva + size:
                return data[offset + address - base - rva:offset + address - base - rva + count]
        raise ValueError('Original range outside PE')

    evidence = args.ida_evidence / 'ida-functions-and-sample.json'
    saved = json.loads(evidence.read_text('utf-8'))
    if saved['survey']['metadata']['sha256'] != SHA:
        raise ValueError('Saved IDA sample differs')
    cs = Cs(CS_ARCH_X86, CS_MODE_32)
    cs.detail = True
    start, end = 0x108593D0, 0x108595C1
    code = raw(start, end - start)
    decoded = list(cs.disasm(code, start))
    rows = saved['coreDisasm']['asm']['lines']
    if len(decoded) != 120 or [x.address for x in decoded] != [int(row['addr'], 16) for row in rows]:
        raise ValueError('Complete original IDA instruction boundaries differ')
    for row in saved['coreBytes']['result']:
        body = bytes(int(word, 16) for word in row['data'].split())
        if body != raw(int(row['addr'], 16), len(body)):
            raise ValueError('Original differs from saved IDA bytes')
    # Bind the reference defaults only after checking their actual original
    # initializer instructions, rather than guessing values from audio names.
    for address, value in [(0x100CF783, .1), (0x100CF873, 11), (0x100CF963, 3)]:
        if raw(address, 7) != bytes.fromhex('c7 45 fc') + struct.pack('<f', value):
            raise ValueError('Original recovery initializer changed')
    if raw(0x11791DD0, 4) != struct.pack('<f', 100):
        raise ValueError('Original recovery maximum changed')
    recorded = {int(item['addr'], 16): item for item in saved['functions']}
    for address in (0x100CF770, 0x100CF860, 0x100CF950):
        original = list(cs.disasm(raw(address, 70), address))
        if [item.address for item in original] != [int(row['addr'], 16)
                for row in recorded[address]['asm']['lines']]:
            raise ValueError('Original initializer IDA boundaries differ')
    globals_ = {
        0x11EE4B3C: 0, 0x11EE4C5C: 0, 0x11EE4CA4: 0,
        0x11EE4B58: 1, 0x11EE4C78: 1, 0x11EE4CC0: 1,
        0x11EE4B40: 2, 0x122788F4: 3, 0x11EE4C60: 4, 0x11791DD0: 5,
        0x11EE4CA8: 6, 0x11EE4B18: 7, 0x11EE4C38: 7, 0x11EE4C80: 7,
        0x117DFFE4: 8, 0x117E000C: 9,
    }
    calls = {0x115306A0: 1, 0x11516C40: 2, 0x11592754: 3}
    fixups = []
    for instruction in decoded:
        offset = instruction.address - start
        for operand in instruction.operands:
            if operand.type == X86_OP_MEM and not operand.mem.base and not operand.mem.index:
                if operand.mem.disp not in globals_ or instruction.disp_size != 4:
                    raise ValueError('Unbound original global')
                fixups.append((offset + instruction.disp_offset, 0, globals_[operand.mem.disp]))
        if instruction.mnemonic == 'call' and instruction.operands[0].type == X86_OP_IMM:
            target = instruction.operands[0].imm
            if target not in calls or instruction.imm_size != 4:
                raise ValueError('Unbound original direct call')
            fixups.append((offset + instruction.imm_offset, calls[target], 0))
        elif instruction.mnemonic == 'push' and instruction.operands[0].type == X86_OP_IMM:
            value = instruction.operands[0].imm
            if value in globals_:
                fixups.append((offset + instruction.imm_offset, 0, globals_[value]))
        elif instruction.mnemonic.startswith('j') and not start <= instruction.operands[0].imm < end:
            raise ValueError('Original branch escapes function')
    header = '// Private original instruction oracle; never publish.\n'
    header += 'inline constexpr unsigned char OriginalRecovery[]{' + ','.join(hex(b) for b in code) + '};\n'
    header += 'struct Fixup { unsigned offset,kind,target; };\ninline constexpr Fixup OriginalFixups[]{' + ','.join(
        '{' + ','.join(map(str, fix)) + '}' for fix in fixups) + '};\n'
    source = Path(__file__).with_name('main.cpp')
    paths = [Path(__file__), source, ROOT / 'native/csol/scenario/custom/standalone_stamina.hpp',
             ROOT / 'native/csol/scenario/custom/standalone_stamina_state.hpp',
             ROOT / 'native/csol/scenario/custom/jump_prediction.hpp',
             ROOT / 'native/csol/scenario/custom/stamina.hpp', ROOT / 'native/csol/scenario/custom/phase.hpp',
             args.original, evidence, args.compiler, args.environment]
    before = [identity(path) for path in paths]
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    out.mkdir(parents=True)
    (out / 'original_recovery.inc').write_text(header, 'utf-8')
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
            text = (result.stdout + result.stderr).decode('utf-8', 'replace')
            (out / (name + '.log')).write_text(text, 'utf-8')
            steps.append(dict(name=name, command=argv, exit=result.returncode))
            print(name, result.returncode, text[-5000:], flush=True)
            if result.returncode:
                raise RuntimeError(name)
        success = True
    finally:
        stable = before == [identity(path) for path in paths]
        (out / 'receipt.json').write_text(json.dumps(dict(success=success and stable, inputsUnchanged=stable,
            inputs=before, steps=steps, sourceCopies=0, deployed=False, gameplay=False, performance=False,
            originalWindow=dict(start=hex(start), endExclusive=hex(end)),
            boundary='Complete actual CSOL recovery function over explicit tweak globals, clock/player/audio boundaries; no run permission/speed/input/PM/client lifecycle acceptance'), indent=2) + '\n', 'utf-8')
    if not success or not stable:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
