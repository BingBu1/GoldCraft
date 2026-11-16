#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the verified hw10210 unchanged-string branch on reused snapshots."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX,
                              UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_ESP)

ROOT = Path(__file__).resolve().parents[2]
SHA = '9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original', 'evidence', 'parser-evidence', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    original, evidence, out = (p.resolve() for p in (args.original, args.evidence, args.output))
    if (not original.is_relative_to(ROOT / 'analysis') or not evidence.is_relative_to(ROOT / 'analysis') or
            not out.is_relative_to(ROOT / 'build') or out.exists()):
        raise ValueError('Use private workspace evidence and a fresh build output')
    spec = importlib.util.spec_from_file_location('pe_reader', ROOT / 'tools/Inspect-MetaHookRuntime.py')
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    pe = reader.PE(original)
    if pe.sha256 != SHA:
        raise ValueError('Unmatched native engine')
    saved = json.loads(evidence.read_text('utf-8'))['result']['structuredContent']['asm']['lines']
    start, finish = 0x101C01A4, 0x101C029C
    code = pe.data[pe.offset(start - 0x10000000):pe.offset(0x1C01CC)]
    decoded = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(code, start))
    expected = [int(row['addr'], 16) for row in saved if start <= int(row['addr'], 16) < 0x101C01CC]
    if not decoded or [row.address for row in decoded] != expected:
        raise ValueError('Original instructions differ from the IDA branch evidence')
    parser_evidence = args.parser_evidence.resolve()
    if not parser_evidence.is_relative_to(ROOT / 'analysis'):
        raise ValueError('Parser evidence must remain private')
    parser_saved = json.loads(parser_evidence.read_text('utf-8'))['result']['structuredContent']['asm']['lines']
    parser_start, parser_end = 0x101D56C0, 0x101D57AD
    parser_code = pe.data[pe.offset(parser_start - 0x10000000):pe.offset(parser_end - 0x10000000)]
    parser_decoded = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(parser_code, parser_start))
    if [row.address for row in parser_decoded] != [int(row['addr'], 16) for row in parser_saved]:
        raise ValueError('Original parser differs from IDA evidence')
    parser_machine = Uc(UC_ARCH_X86, UC_MODE_32)
    parser_machine.mem_map(0x10000000, 0x1500000)
    for section in pe.sections:
        parser_machine.mem_write(0x10000000 + section['rva'],
                                 pe.data[section['raw']:section['raw'] + section['size']])
    parser_machine.mem_map(0x300000, 0x6000)
    # Only the verified parser executes. Its external strcmp and security-cookie
    # callees use deterministic, allocation-free ABI stubs in this oracle.
    parser_machine.mem_write(0x101BB130, b'\xc3')
    parser_machine.mem_write(0x102AB707, b'\xc3')

    def string_at(address):
        return bytes(parser_machine.mem_read(address, 256)).split(b'\0', 1)[0]

    def external_call(machine, address, size, user):
        if address == 0x101BB130:
            stack = machine.reg_read(UC_X86_REG_ESP)
            a, b = struct.unpack('<II', machine.mem_read(stack + 4, 8))
            av, bv = string_at(a), string_at(b)
            machine.reg_write(UC_X86_REG_EAX, (av > bv) - (av < bv))

    parser_machine.hook_add(UC_HOOK_CODE, external_call, begin=0x101BB130, end=0x101BB130)

    def read_key(value, key=b'cso_jdeny'):
        parser_machine.mem_write(0x300000, value + b'\0' + bytes(256))
        parser_machine.mem_write(0x301000, key + b'\0')
        parser_machine.mem_write(0x304000, struct.pack('<III', 0x305000, 0x300000, 0x301000))
        parser_machine.reg_write(UC_X86_REG_ESP, 0x304000)
        parser_machine.emu_start(parser_start, 0x305000, count=10000)
        return string_at(parser_machine.reg_read(UC_X86_REG_EAX))
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(0x101C0000, 0x1000)
    machine.mem_write(start, code)
    machine.mem_map(0x200000, 0x4000)
    source, destination, descriptor, frame = 0x200000, 0x201000, 0x202000, 0x203000
    field, size = 140, 476
    machine.mem_write(descriptor + 36, struct.pack('<I', field))
    machine.mem_write(frame + 8, struct.pack('<IIII', source, destination, descriptor, size))

    def replay(value, previous):
        machine.mem_write(source, bytes(size))
        machine.mem_write(destination, bytes(size))
        machine.mem_write(source + field, value + b'\0')
        machine.mem_write(destination + field, previous + b'\0')
        for register, value in ((UC_X86_REG_EBP, frame), (UC_X86_REG_EBX, field),
                                (UC_X86_REG_ESI, 0), (UC_X86_REG_EDI, descriptor)):
            machine.reg_write(register, value)
        machine.emu_start(start, finish, count=10000)
        return bytes(machine.mem_read(destination + field, 256)).split(b'\0', 1)[0]

    base = b'\\slj\\0\\hl\\1'
    denied, allowed = base + b'\\cso_jdeny\\1', base + b'\\cso_jdeny\\0'
    legacy = replay(base, denied)
    if legacy != denied:
        raise AssertionError('Original branch did not reproduce the observed stale denial')
    checks = 1
    for prefix in (b'', base, b'\\ordinary\\retained', base + b'\\cso_pass\\00000001'):
        values = [prefix + b'\\cso_jdeny\\' + bit for bit in (b'0', b'1')]
        for prior in values:
            for current in values:
                for _ in range(32):
                    if replay(current, prior) != current:
                        raise AssertionError('Explicit authority changed during native unchanged replay')
                    checks += 1
    # Other scenario fields may disappear before the denial field. Execute the
    # real decoder with a longer reused destination, not just equal-size states.
    longer = base + b'\\cso_pass\\00000001'
    current = base + b'\\cso_jdeny\\1'
    prior = longer + b'\\cso_jdeny\\0'
    prefix_result = replay(current, prior)
    if prefix_result == current or read_key(prefix_result) == b'1':
        raise AssertionError('Cross-prefix reuse did not reproduce the malformed denial value')
    prefix_checks = 0
    prefixes = (b'', base, longer, b'\\ordinary\\retained',
                b'\\ordinary\\' + b'x' * 96 + b'\\cso_pass\\12345678')
    for prefix in prefixes:
        for previous_prefix in prefixes:
            for bit in (b'0', b'1'):
                for previous_bit in (b'0', b'1'):
                    value = prefix + b'\\cso_jdeny\\' + bit + b'\\'
                    old = previous_prefix + b'\\cso_jdeny\\' + previous_bit + b'\\'
                    actual = replay(value, old)
                    if read_key(actual) != bit:
                        raise AssertionError('Delimited authority changed after native replay')
                    for key in (b'slj', b'hl', b'ordinary', b'cso_pass'):
                        if read_key(prefix, key) and read_key(actual, key) != read_key(prefix, key):
                            raise AssertionError('Existing physics key changed')
                    prefix_checks += 1
    out.mkdir(parents=True)
    receipt = dict(success=True, checks=checks, originalSha256=SHA, branchVA=hex(start),
                   instructionCount=len(decoded), codeSha256=hashlib.sha256(code).hexdigest(),
                   legacyStaleDenialReproduced=True, explicitStatePreserved=True,
                   prefixChange=dict(previous=prior.decode(), current=current.decode(),
                                     actual=prefix_result.decode(), staleSuffixReproduced=True),
                   parserVA=hex(parser_start), parserInstructions=len(parser_decoded),
                   parserCodeSha256=hashlib.sha256(parser_code).hexdigest(),
                   crossPrefixChecks=prefix_checks, delimiterStatePreserved=True,
                   sourceUnchanged=hashlib.sha256(original.read_bytes()).hexdigest() == SHA,
                   boundary='Actual original branch execution; not whole-engine, gameplay or frame-time acceptance')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf-8')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
