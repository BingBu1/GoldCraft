# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate cold model preparation against the actual original duration function."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_MXCSR, UC_X86_REG_FPCW

ROOT = Path(__file__).resolve().parents[2]
SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
BASE, MODEL, STACK, STOP = 0x10000000, 0x20000000, 0x30000000, 0x40000000


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Original:
    def __init__(self, sample, proof):
        data = sample.read_bytes()
        if sha(sample) != SHA or proof['sampleSha256'] != SHA:
            raise ValueError('Original module identity differs')
        nt = struct.unpack_from('<I', data, 60)[0]
        if data[nt:nt+4] != b'PE\0\0' or struct.unpack_from('<H', data, nt+4)[0] != 0x14c:
            raise ValueError('Original must be PE32 x86')
        count = struct.unpack_from('<H', data, nt+6)[0]
        opt_size = struct.unpack_from('<H', data, nt+20)[0]
        opt = nt+24
        if struct.unpack_from('<I', data, opt+28)[0] != BASE:
            raise ValueError('Original image base differs')
        image = bytearray(struct.unpack_from('<I', data, opt+56)[0])
        for i in range(count):
            va, size, raw = struct.unpack_from('<III', data, opt+opt_size+40*i+12)
            image[va:va+size] = data[raw:raw+size]
        evidence = next(x for x in proof['regions'] if int(x['va'], 16) == 0x1000f390)
        expected = bytes.fromhex(evidence['hex'])
        if not expected or hashlib.sha256(expected).hexdigest() != evidence['sha256'] or image[0xf390:0xf390+len(expected)] != expected:
            raise ValueError('IDA/original duration bytes differ')
        self.region = evidence['sha256']
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(BASE, (len(image)+4095) & ~4095)
        self.uc.mem_write(BASE, bytes(image))
        for address in (MODEL, STACK, STOP):
            self.uc.mem_map(address, 4096)
        # Only a caller-side fstp adapter is synthesized. The complete original
        # function runs unchanged and must reach this return address.
        self.uc.mem_write(STOP, b'\xd9\x1d' + struct.pack('<I', STOP+0x100))

    def duration(self, frames, fps):
        self.uc.mem_write(MODEL+164, struct.pack('<ii', 1, 244))
        self.uc.mem_write(MODEL+244+32, struct.pack('<I', fps))
        self.uc.mem_write(MODEL+244+56, struct.pack('<i', frames))
        self.uc.mem_write(STACK+0x800, struct.pack('<III', STOP, MODEL, 0))
        self.uc.reg_write(UC_X86_REG_ESP, STACK+0x800)
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        self.uc.reg_write(UC_X86_REG_FPCW, 0x27f)
        self.uc.emu_start(0x1000f390, STOP+6, count=100)
        return struct.unpack('<I', self.uc.mem_read(STOP+0x100, 4))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sample', 'ida-bytes', 'environment', 'models', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output.absolute()
    for part in (output, *output.parents):
        if part.is_symlink() or os.path.isjunction(part):
            raise ValueError('Output may not traverse links')
    if output.exists() or not output.resolve().is_relative_to(ROOT/'build'):
        raise ValueError('Use a fresh workspace build directory')
    proof = json.loads(args.ida_bytes.read_text('utf-8'))
    original = Original(args.sample, proof)
    names = ('glock', 'p345', 'revolver', 'vp70')
    models = [args.models/name/('v_'+name+'.mdl') for name in names]
    inputs = [ROOT/'native/guest/cof/magazine_model.cpp', ROOT/'native/guest/cof/magazine_model.hpp',
              ROOT/'native/guest/cof/magazine_actions.hpp', ROOT/'native/guest/cof/scenario_damage.hpp',
              Path(__file__).resolve(), Path(__file__).with_name('main.cpp'),
              ROOT/'external/ReGameDLL_CS/regamedll/engine/studio.h', *models]
    before = {str(p): sha(p) for p in inputs}
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    config = json.loads((ROOT/'.tools/clang-toolchain.json').read_text('utf-8-sig'))
    compiler = Path(config['root'])/'bin/clang-cl.exe'
    env = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    output.mkdir(parents=True)
    temp = output/'temp'
    temp.mkdir()
    env.update(TEMP=str(temp), TMP=str(temp))
    exe = output/'magazine-model.exe'
    command = [str(compiler), '/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc',
               '/MT', '/utf-8', '/W4', '/WX', '/O2', '/clang:-O3', '/clang:-flto=thin',
               '/clang:-fuse-ld=lld', '/arch:AVX2', '/fp:precise', '/clang:-ffp-contract=off',
               '-Wno-overriding-option',
               '/D_CRT_SECURE_NO_WARNINGS', '/I'+str(ROOT),
               '/I'+str(ROOT/'external/ReGameDLL_CS/regamedll/common'), str(inputs[0]), str(inputs[5]),
               '/Fe:'+str(exe), '/link', '/machine:x86', '/threads:1']
    run = subprocess.run(command, cwd=output, env=env, capture_output=True, text=True, timeout=90)
    (output/'compile.log').write_text(run.stdout+run.stderr, 'utf-8')
    if run.returncode:
        print(run.stdout+run.stderr)
    run.check_returncode()
    safety = subprocess.run([str(exe), '--safety'], cwd=output, capture_output=True, text=True, timeout=10)
    (output/'safety.log').write_text(safety.stdout+safety.stderr, 'utf-8')
    safety.check_returncode()
    rng = random.Random(0xc0f345)
    cases = [(frames, struct.unpack('<I', struct.pack('<f', fps))[0])
             for frames in (1, 2, 3, 31, 16777217, 2147483647)
             for fps in (.01, .1, 1, 29.97, 30, 60, 480)]
    cases += [(rng.randrange(1, 1000000), struct.unpack('<I', struct.pack('<f', rng.uniform(.01, 480)))[0])
              for _ in range(4096)]
    expected = [original.duration(*case) for case in cases]
    run = subprocess.run([str(exe), '--oracle'], cwd=output, capture_output=True, text=True,
                         input=''.join(f'{frames} {fps:08x}\n' for frames, fps in cases), timeout=30)
    (output/'oracle.log').write_text(run.stderr, 'utf-8')
    rows = [tuple(int(x, 16) for x in line.split()) for line in run.stdout.splitlines()]
    if run.returncode or len(rows) != len(cases) or any(status != 0 or value != wanted for (status, value), wanted in zip(rows, expected)):
        raise ValueError('Original duration comparison failed')
    wanted = []
    for m, model in enumerate(models):
        data = model.read_bytes()
        count, start = struct.unpack_from('<ii', data, 164)
        for i in range(count):
            fps = struct.unpack_from('<I', data, start+176*i+32)[0]
            frames = struct.unpack_from('<i', data, start+176*i+56)[0]
            wanted.append((m, i, original.duration(frames, fps)))
    run = subprocess.run([str(exe), '--models', *map(str, models)], cwd=output, capture_output=True, text=True, timeout=20)
    actual = [(int(m), int(i), int(bits, 16)) for m, i, bits in (s.split() for s in run.stdout.splitlines())]
    (output/'models.log').write_text(run.stdout+run.stderr, 'utf-8')
    if run.returncode or actual != wanted:
        raise ValueError('Actual model/original duration comparison failed')
    after = {str(p): sha(p) for p in inputs}
    if before != after:
        raise ValueError('Inputs changed during validation')
    receipt = dict(success=True, revision=revision, command=command, sampleSha256=SHA,
                   regionSha256=original.region, before=before, after=after, unchanged=True,
                   compilerSha256=sha(compiler), originalCases=len(cases), actualModelSequences=len(wanted),
                   safety=safety.stdout.strip(), sourceCopies=0, gameplay=False, performance=False)
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print(json.dumps({k: receipt[k] for k in ('success', 'originalCases', 'actualModelSequences', 'safety')}))


if __name__ == '__main__':
    main()
