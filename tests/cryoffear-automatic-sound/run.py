#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay original automatic audio windows through the actual native wrapper.

This does not execute complete PrimaryAttack, ItemPostFrame or host transport.
No original binary bytes are embedded in the published test.
"""
import argparse
import hashlib
import importlib.util
import itertools
import json
import os
from pathlib import Path
import struct
import subprocess

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_EAX, UC_X86_REG_MXCSR

ROOT = Path(__file__).resolve().parents[2]
SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
BASE, WORK, STACK, STUB = 0x10000000, 0x30000000, 0x40008000, 0x50000000


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


class Original:
    def __init__(self, image):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(BASE, (len(image) + 4095) & ~4095)
        self.uc.mem_write(BASE, bytes(image))
        self.uc.mem_map(WORK, 0x20000)
        self.uc.mem_map(0x40000000, 0x10000)
        self.uc.mem_map(STUB, 0x1000)
        self.owner, self.pev, self.globals = WORK + 0x1000, WORK + 0x6000, WORK + 0x7000
        self.strings = WORK + 0xB000
        self.write(WORK + 0x160, self.owner)
        self.write(self.owner, WORK + 0x9000)
        self.write(self.owner + 4, self.pev)
        self.write(self.pev + 0x20C, WORK + 0xC000)
        self.write(WORK + 0x9000 + 0xBC, 0x10013560)
        self.write(0x10223188, self.globals)
        self.write(self.globals + 0x98, self.strings)
        self.write(0x10222EE4, WORK + 0x8000)
        self.write(WORK + 0x8000, WORK + 0xA000)
        self.write(WORK + 0xA000 + 0x20, STUB + 0x20)
        self.write(0x10222FBC, STUB)
        self.uc.mem_write(self.strings + 0x100, b'weapons/tmp/fire.wav\0')
        self.uc.mem_write(self.strings + 0x180, b'weapons/tmp/tail.wav\0')
        # Exact original security-cookie checker is outside audio semantics.
        call = 0x100FCAE3
        assert image[call - BASE] == 0xE8
        self.cookie = call + 5 + struct.unpack_from('<i', image, call - BASE + 1)[0]
        self.visited = set()
        self.uc.hook_add(UC_HOOK_CODE, self.hook)

    def write(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value & 0xFFFFFFFF))

    def read(self, address):
        return struct.unpack('<I', self.uc.mem_read(address, 4))[0]

    def byte(self, address, value=None):
        if value is None:
            return self.uc.mem_read(address, 1)[0]
        self.uc.mem_write(address, bytes([value]))

    def ret(self, value=None):
        sp = self.uc.reg_read(UC_X86_REG_ESP)
        if value is not None:
            self.uc.reg_write(UC_X86_REG_EAX, value)
        self.uc.reg_write(UC_X86_REG_ESP, sp + 4)
        self.uc.reg_write(UC_X86_REG_EIP, self.read(sp))

    def hook(self, uc, address, size, user):
        self.visited.add(address)
        if address == self.end:
            uc.emu_stop()
        elif address == STUB + 0x20:
            self.ret(1)  # Authenticated original IsMultiplayer branch.
        elif address == self.cookie:
            self.ret()
        elif address == STUB:
            sp = uc.reg_read(UC_X86_REG_ESP)
            args = [self.read(sp + 4 + i * 4) for i in range(7)]
            assert args[0] == WORK + 0xC000
            sample = {self.strings + 0x100: 0, self.strings + 0x180: 1}[args[2]]
            self.events.append([sample, args[1], args[5], args[6], args[3], args[4], self.byte(WORK + 0x1A5)])
            self.ret(0)
        elif not BASE <= address < BASE + 0x263000:
            raise ValueError('Unexpected original external instruction: ' + hex(address))

    def run(self, row):
        op, playing, clip, action, loading, alive, held, quiet, fire, tail = row
        self.byte(WORK + 0x1A5, playing)
        self.write(WORK + 0x1E4, clip)
        self.write(WORK + 0x128, action)
        self.write(WORK + 0x1F0, loading)
        self.write(WORK + 0x1A8, 0x100 if fire else 0)
        self.write(WORK + 0x1AC, 0x180 if tail else 0)
        self.write(self.pev + 0x174, 0 if alive else 1)
        self.write(self.pev + 0x164, 0x42C80000 if alive else 0)
        self.write(self.pev + 0x184, held)
        self.byte(0x10226489, quiet)
        start, self.end = ((0x1007F788, 0x1007F82E) if op == 0 else
                          (0x1005BA23, 0x1005BAC9) if op == 1 else (0x1011D6BA, 0x1011D81B))
        self.events = []
        self.uc.reg_write(UC_X86_REG_ESP, STACK)
        self.uc.reg_write(UC_X86_REG_EBP, STACK + 0x100)
        self.uc.reg_write(UC_X86_REG_EDI, WORK)
        self.uc.reg_write(UC_X86_REG_ECX, WORK)
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        self.uc.emu_start(start, self.end, count=1000)
        assert self.uc.reg_read(UC_X86_REG_EIP) == self.end
        return [self.byte(WORK + 0x1A5), len(self.events), *itertools.chain.from_iterable(self.events)]


def original_image(sample, proof_path):
    if sha(sample) != SHA:
        raise ValueError('Original module identity mismatch')
    helper = ROOT / 'tests/cryoffear-magazine-actions/run.py'
    shared = load('automatic_original_image', helper)
    image = shared.mapped_image(sample.read_bytes())
    proof = json.loads(proof_path.read_text('utf8'))
    required = {0x1007F788, 0x1005BA23, 0x1011D560, 0x1001E990, 0x100FC9F0, 0x10013560, 0x101A4E64}
    if proof['sampleSha256'] != SHA:
        raise ValueError('IDA module identity mismatch')
    for address in required:
        row = next(row for row in proof['regions'] if int(row['va'], 16) == address)
        data = bytes.fromhex(row['hex'])
        assert data and hashlib.sha256(data).hexdigest() == row['sha256']
        assert image[address - BASE:address - BASE + len(data)] == data
    return image


def cases():
    return [[operation, playing, clip, action, loading, alive, held, quiet, fire, tail]
        for operation in range(3)
        for playing, clip, action, loading, alive, held, quiet, fire, tail in itertools.product(
            (0, 1), (0, 1, 30, 75), (0, 1, 2, 5), (0, 1), (0, 1), (0, 1), (0, 1), (0, 1), (0, 1))]


def generate(sample, proof, case_output, expected_output):
    original = Original(original_image(sample, proof))
    rows = cases()
    for row in rows:
        case_output.write(' '.join(f'{word:08x}' for word in row) + '\n')
        expected_output.write(' '.join(f'{word:08x}' for word in original.run(row)) + '\n')
    return dict(sampleSha256=SHA, cases=len(rows), visitedInstructions=len(original.visited),
                proofSha256=sha(proof), boundary='Original native automatic-audio windows with original stop/quiet wrapper')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sample', 'proof', 'environment', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.absolute()
    if out.exists() or not out.resolve().is_relative_to(ROOT / 'build') or any(p.is_symlink() or p.is_junction() for p in (out, *out.parents)):
        raise ValueError('Use a fresh workspace build directory')
    helper = ROOT / 'tests/cryoffear-magazine-actions/run.py'
    image = original_image(args.sample, args.proof)
    source = ROOT / 'native/guest/cof/automatic_sound.cpp'
    fixture = Path(__file__).with_name('main.cpp')
    compiler = Path(json.loads((ROOT / '.tools/clang-toolchain.json').read_text('utf-8-sig'))['root']) / 'bin/clang-cl.exe'
    inputs = [source, source.with_suffix('.hpp'), fixture, Path(__file__), helper, args.proof, args.environment,
              compiler, compiler.with_name('lld-link.exe')]
    before = {str(path): sha(path) for path in inputs}
    out.mkdir()
    (out / 'temp').mkdir()
    env = os.environ.copy()
    env.update(json.loads(args.environment.read_text('utf-8-sig')))
    env['TEMP'] = env['TMP'] = str(out / 'temp')
    exe = out / 'automatic-sound.exe'
    command = [str(compiler), '/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/utf-8',
        '/W4', '/WX', '/O2', '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/arch:AVX2',
        '/fp:precise', '/clang:-ffp-contract=off', '-Wno-overriding-option', '/I' + str(ROOT),
        str(source), str(fixture), '/Fe:' + str(exe), '/link', '/threads:1']
    built = subprocess.run(command, cwd=out, env=env, capture_output=True)
    (out / 'compile.log').write_bytes(built.stdout + built.stderr)
    built.check_returncode()
    safety = subprocess.run([str(exe), '--safety'], cwd=out, capture_output=True)
    (out / 'safety.log').write_bytes(safety.stdout + safety.stderr)
    safety.check_returncode()
    oracle = Original(image)
    rows = cases()
    expected = [oracle.run(row) for row in rows]
    actual = subprocess.run([str(exe)], cwd=out, input=''.join(' '.join(map(str, row)) + '\n' for row in rows),
                            text=True, capture_output=True, timeout=30)
    outputs = [list(map(int, line.split())) for line in actual.stdout.splitlines()]
    failures = [dict(case=case, expected=want, actual=got) for case, want, got in zip(rows, expected, outputs) if want != got]
    stable = before == {str(path): sha(path) for path in inputs}
    success = stable and actual.returncode == 0 and len(outputs) == len(rows) and not failures
    receipt = dict(success=success, sampleSha256=SHA, originalCases=len(rows), actualRows=len(outputs),
        visitedInstructions=len(oracle.visited), failures=failures[:12], safety=safety.stdout.decode().strip(),
        inputsUnchanged=stable, before=before, command=command, sourceCopies=0,
        boundary='Original native automatic-audio windows and actual stop/quiet wrappers; intercepted native EmitSound/cookie/rules result. No full weapon, engine network, playback, multiplayer or performance acceptance.')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf8')
    print(json.dumps({key: receipt[key] for key in ('success', 'originalCases', 'actualRows', 'safety', 'failures')}))
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
