# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the production magazine helper with matched original x86 bytes.

Requires the authorized private hl.dll and its IDA byte receipt. Nothing from
the original binary is embedded in the published test or copied into source.
"""
import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import struct
import subprocess

from unicorn import Uc, UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parents[2]
SAMPLE_SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
BASE, SIZE = 0x10000000, 0x300000
WEAPON, PLAYER, GLOBALS, STACK = 0x100000, 0x110000, 0x120000, 0x140000
START, STOP, CLOCK, NOTIFY = 0x1011d91f, 0x1011d9b6, 0x1011ab20, 0x100f5eb0


def u32(data, at):
    return struct.unpack_from('<I', data, at)[0]


def image(data):
    pe = u32(data, 0x3c)
    if data[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', data, pe+4)[0] != 0x14c:
        raise ValueError('Expected original PE32 x86 sample')
    count, optional = struct.unpack_from('<H', data, pe+6)[0], pe+24
    if u32(data, optional+28) != BASE:
        raise ValueError('Unexpected original image base')
    result = bytearray(SIZE)
    result[:1024] = data[:1024]
    table = optional + struct.unpack_from('<H', data, pe+20)[0]
    for i in range(count):
        at = table + 40*i
        virtual_size, rva, raw_size, raw = struct.unpack_from('<4I', data, at+8)
        if rva + max(virtual_size, raw_size) > SIZE or raw + raw_size > len(data):
            raise ValueError('Original section lies outside image')
        result[rva:rva+raw_size] = data[raw:raw+raw_size]
    return result


class Original:
    def __init__(self, mapped):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(BASE, SIZE)
        self.uc.mem_write(BASE, bytes(mapped))
        self.uc.mem_map(WEAPON, 0x50000)
        self.write(0x10223188, GLOBALS)
        self.write(WEAPON+0x160, PLAYER)
        self.write(WEAPON+0x168, 7)  # verified P345 identity, ordinary magazine path
        self.write(WEAPON+0x1dc, 1)
        self.uc.hook_add(UC_HOOK_CODE, self.hook)

    def write(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value))

    def read(self, address):
        return struct.unpack('<i', self.uc.mem_read(address, 4))[0]

    def hook(self, uc, address, size, user):
        if address == NOTIFY:
            self.notifications.append((self.read(WEAPON+0x1e4), self.read(PLAYER+0x2544), self.read(WEAPON+0x1f0)))
            # Only the external owner notification is stubbed. Its placement
            # and the loading flag visible to it are verified separately.
            sp = uc.reg_read(UC_X86_REG_ESP)
            target = self.read(sp) & 0xffffffff
            uc.reg_write(UC_X86_REG_ESP, sp+4)
            uc.reg_write(UC_X86_REG_EIP, target)

    def run(self, case):
        clip, reserve, capacity, flags, now, deadline, loading = case
        self.notifications = []
        for at, value in ((WEAPON+0x1e4, clip), (PLAYER+0x2544, reserve), (WEAPON+0x1f0, loading),
                          (PLAYER+0x308, deadline), (GLOBALS, now),
                          (0x102318a4+7*44, capacity), (0x102318ac+7*44, flags)):
            self.write(at, value)
        self.uc.reg_write(UC_X86_REG_EDI, WEAPON)
        self.uc.reg_write(UC_X86_REG_EDX, PLAYER)
        self.uc.reg_write(UC_X86_REG_EBP, STACK+0x8000)
        self.uc.reg_write(UC_X86_REG_ESP, STACK+0x7000)
        self.uc.emu_start(START, STOP, count=100)
        if self.uc.reg_read(UC_X86_REG_EIP) != STOP:
            raise ValueError('Original execution did not reach the inspected boundary')
        result = (self.read(WEAPON+0x1e4), self.read(PLAYER+0x2544), self.read(WEAPON+0x1f0))
        committed = bool(self.notifications)
        observed = self.notifications[0] if committed else (-1, -1, -1)
        return (*result, int(committed), result[0]-clip, len(self.notifications), *observed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sample', type=Path, required=True)
    parser.add_argument('--ida-bytes', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or not output.is_relative_to(ROOT/'build'):
        raise ValueError('Use a new build directory')
    output.mkdir(parents=True)
    inputs = [Path(__file__).resolve(), ROOT/'tests/cryoffear-magazine/main.cpp',
              ROOT/'native/guest/cof/magazine_reload.cpp', ROOT/'native/guest/cof/magazine_reload.hpp']
    before = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    sample = args.sample.read_bytes()
    if hashlib.sha256(sample).hexdigest() != SAMPLE_SHA:
        raise ValueError('Original sample identity differs')
    mapped = image(sample)
    evidence = json.loads(args.ida_bytes.read_text('utf-8'))
    if evidence['sampleSha256'] != SAMPLE_SHA:
        raise ValueError('IDA evidence identity differs')
    for address in (START, CLOCK):
        row = next(r for r in evidence['regions'] if int(r['va'], 16) == address)
        raw = bytes.fromhex(row['hex'])
        if hashlib.sha256(raw).hexdigest() != row['sha256'] or mapped[address-BASE:address-BASE+len(raw)] != raw:
            raise ValueError('IDA/original bytes differ')
    environment = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    executable = output/'cof-magazine.exe'
    command = [str(args.compiler), '/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc',
               '/MT', '/utf-8', '/O2', '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld',
               '/arch:AVX2', '/fp:precise', '/I'+str(ROOT), str(inputs[1]), str(inputs[2]),
               '/Fe:'+str(executable), '/link', '/machine:x86', '/threads:1']
    compiled = subprocess.run(command, env=environment, cwd=output, capture_output=True, text=True, timeout=120)
    (output/'compile.log').write_text(compiled.stdout+compiled.stderr, 'utf-8')
    if compiled.returncode:
        raise RuntimeError('Compile failed; see compile.log')
    oracle = Original(mapped)
    cases, expected = [], []
    for capacity in (8, 15, 18):
        for clip, reserve, flags, loading, offset in itertools.product(
                range(capacity+1), (0, 1, capacity-1, capacity, capacity+3, 151, 180, 500, 800, 65535, 2147483647), (0, 0x20), (0, 1), (-1, 0, 1)):
            deadline = u32(struct.pack('<f', 10.25), 0)
            case = (clip, reserve, capacity, flags, deadline+offset, deadline, loading)
            cases.append(case)
            expected.append(oracle.run(case))
    run = subprocess.run([str(executable)], cwd=output, input=''.join(' '.join(map(str,c))+'\n' for c in cases),
                         capture_output=True, text=True, timeout=30)
    actual = [tuple(map(int, line.split())) for line in run.stdout.splitlines()]
    failures = [dict(case=c, expected=e, actual=a) for c,e,a in zip(cases,expected,actual) if e != a]
    success = run.returncode == 0 and len(actual) == len(expected) and not failures
    after = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    success = success and before == after
    receipt = dict(sampleSha256=SAMPLE_SHA, start=hex(START), stop=hex(STOP), cases=len(cases),
                   compiled=compiled.returncode, run=run.returncode, rows=len(actual), failures=failures[:10],
                   command=command, before=before, after=after, unchanged=before==after, success=success,
                   boundary='Ordinary magazine completion and notification ordering only; no weapon integration or gameplay/performance acceptance')
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print(json.dumps({k:receipt[k] for k in ('cases','rows','failures','success')}))
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
