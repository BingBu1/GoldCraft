# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the production cold resource helper against the actual ReGameDLL SDK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / 'external/ReGameDLL_CS/regamedll'
SAMPLE_SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_original(sample, proof_path):
    data = sample.read_bytes()
    proof = json.loads(proof_path.read_text('utf-8'))
    if sha(sample) != SAMPLE_SHA or proof['sampleSha256'] != SAMPLE_SHA:
        raise ValueError('Original module identity differs')
    nt = struct.unpack_from('<I', data, 60)[0]
    if data[nt:nt+4] != b'PE\0\0' or struct.unpack_from('<H', data, nt+4)[0] != 0x14c:
        raise ValueError('Original is not PE32 x86')
    opt, count = nt + 24, struct.unpack_from('<H', data, nt+6)[0]
    if struct.unpack_from('<I', data, opt+28)[0] != 0x10000000:
        raise ValueError('Original base differs')
    sections = opt + struct.unpack_from('<H', data, nt+20)[0]
    rows = []
    for row in proof['regions']:
        found = None
        for i in range(count):
            rva, size, raw = struct.unpack_from('<III', data, sections+40*i+12)
            if rva <= row['rva'] and row['rva']+row['size'] <= rva+size:
                found = data[raw+row['rva']-rva:raw+row['rva']-rva+row['size']]
                break
        if found is None or hashlib.sha256(found).hexdigest() != row['sha256'] or not row['idaEqualsPE']:
            raise ValueError('Stored original IDA window differs')
        rows.append(row)
    if not rows:
        raise ValueError('Empty original gate')
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sample', 'original', 'clip', 'environment', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output.absolute()
    for part in (output, *output.parents):
        if part.is_symlink() or os.path.isjunction(part):
            raise ValueError('Output traverses a link')
    if output.exists() or not output.resolve().is_relative_to(ROOT/'build'):
        raise ValueError('Use a fresh workspace build directory')
    proof = ROOT/'analysis/cof-weapons-20261111/server-evidence/p345-byte-verification.json'
    rows = verify_original(args.sample, proof)
    metadata = args.original/'inventoryitems/weapons/weapon_p345.txt'
    if sha(metadata) != '376a05dad38387981be4a7f88a1a6444582f5781afd209df9a3d5a7ea18bb2fe':
        raise ValueError('Original inventory metadata identity differs')
    sources = [ROOT/'native/csol/server/weapons/guest_cof_resources.cpp',
               ROOT/'native/guest/cof/magazine_model.cpp', Path(__file__).with_name('main.cpp')]
    header = ROOT/'native/csol/server/weapons/guest_cof_resources.hpp'
    pairs = re.findall(r'\{"([^"]+)", "([^"]+)"\}', header.read_text('utf-8'))
    resources = []
    for original, host in pairs:
        if original == 'models/w_9mmclip.mdl':
            source = args.clip
        else:
            source = args.original/('sound/' if original.endswith('.wav') else '')/original
        resources.append(dict(original=original, host=host, path=str(source.resolve()), sha256=sha(source)))
    for original in ('inventoryitems/weapons/weapon_p345.txt', 'aurora/gunsmoke.aur'):
        path = args.original/original
        resources.append(dict(original=original, path=str(path.resolve()), sha256=sha(path)))
    project = SDK/'msvc/ReGameDLL.vcxproj'
    inputs = set(sources) | {header, Path(__file__).resolve(), Path(__file__).with_name('README.md'),
        args.sample.resolve(), proof, project, args.environment.resolve(), ROOT/'.tools/clang-toolchain.json',
        ROOT/'external/ReHLDS/rehlds/engine/pr_cmds.cpp', ROOT/'external/ReHLDS/rehlds/engine/sv_main.cpp'}
    inputs.update(Path(r['path']) for r in resources)
    includes = re.search(r'<AdditionalIncludeDirectories>(.*?)</AdditionalIncludeDirectories>',
                         project.read_text('utf-8-sig'))[1].split(';')
    includes = [(project.parent/v[len('$(ProjectDir)'):].replace('\\', '/').lstrip('/')).resolve()
                for v in includes if v.startswith('$(ProjectDir)')]
    config = json.loads((ROOT/'.tools/clang-toolchain.json').read_text('utf-8-sig'))
    compiler = Path(config['root'])/'bin/clang-cl.exe'
    compiler_before = sha(compiler)
    env = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    output.mkdir(parents=True)
    temp = output/'temp'
    temp.mkdir()
    env.update(TEMP=str(temp), TMP=str(temp))
    defs = ('REGAMEDLL_ADD REGAMEDLL_API REGAMEDLL_FIXES REGAMEDLL_SSE REGAMEDLL_SELF '
            'REGAMEDLL_CHECKS UNICODE_FIXES BUILD_LATEST BUILD_LATEST_FIXES CLIENT_WEAPONS '
            'USE_QSTRING _CRT_SECURE_NO_WARNINGS NDEBUG').split()
    flags = ['/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/utf-8',
             '/O2', '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/arch:AVX2',
             '/fp:precise', '/clang:-ffp-contract=off', '/Gy', '/Gw',
             *('/D'+d for d in defs), *('/I'+str(p) for p in includes), '/I'+str(ROOT)]
    commands = []
    for i, source in enumerate(sources):
        dep = output/f'{i}.d'
        command = [str(compiler), *flags, '/clang:-M', '/clang:-MF', '/clang:'+str(dep), str(source)]
        commands.append(command)
        run = subprocess.run(command, cwd=output, env=env, capture_output=True, text=True, timeout=60)
        (output/f'{i}-headers.log').write_text(run.stdout+run.stderr, 'utf-8')
        run.check_returncode()
        make = dep.read_text().replace(chr(92)+chr(10), ' ')
        inputs.update(Path(v.replace(chr(92)+' ', ' ')).resolve()
                      for v in re.findall(r'(?:\\ |[^\s])+', make.split(':', 1)[1]))
    before = {str(p): sha(p) for p in sorted(inputs)}
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    exe = output/'resources.exe'
    command = [str(compiler), *flags, *map(str, sources), '/Fe:'+str(exe),
               '/link', '/machine:x86', '/threads:1', '/opt:ref']
    compile_run = subprocess.run(command, cwd=output, env=env, capture_output=True, text=True, timeout=90)
    (output/'compile.log').write_text(compile_run.stdout+compile_run.stderr, 'utf-8')
    test = None
    if not compile_run.returncode:
        test = subprocess.run([str(exe), str(args.original.resolve()), str(args.clip.resolve())],
                              cwd=output, capture_output=True, text=True, timeout=30)
        (output/'test.log').write_text(test.stdout+test.stderr, 'utf-8')
    after = {str(p): sha(p) for p in sorted(inputs)}
    success = not compile_run.returncode and test is not None and not test.returncode and before == after and compiler_before == sha(compiler)
    receipt = dict(success=success, revision=revision, command=command, dependencyCommands=commands,
        compilerSha256=compiler_before, before=before, after=after, unchanged=before == after,
        original=dict(sha256=SAMPLE_SHA, regions=rows, resourceInputs=resources),
        compiled=compile_run.returncode, tested=test.returncode if test is not None else None,
        sourceCopies=0, boundary='Actual SDK helper and complete real resource bytes with controlled VFS/precache callbacks. No actual engine process, weapon registry, client voice/particle/backend admission, P grip, deployment, gameplay or performance acceptance.',
        sharedClip='Explicit existing workspace valve model for the named host common dependency; not proof of original CoF VFS inheritance.')
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print(test.stdout+test.stderr if test is not None else compile_run.stdout+compile_run.stderr)
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
