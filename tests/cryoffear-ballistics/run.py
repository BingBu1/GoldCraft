# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual SDK adapter versus private original full FireBullets instructions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from oracle import generate

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sample', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build directory')
    output.mkdir(parents=True)
    inputs = [Path(__file__).resolve(), ROOT/'tests/cryoffear-ballistics/oracle.py',
              ROOT/'tests/cryoffear-ballistics/main.cpp', ROOT/'native/guest/cof/server/ballistics.hpp',
              ROOT/'native/guest/cof/server/ballistics.cpp',
              ROOT/'external/ReGameDLL_CS/regamedll/dlls/util.cpp',
              ROOT/'external/ReGameDLL_CS/regamedll/msvc/ReGameDLL.vcxproj']

    def identities():
        return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}

    before = identities()
    with (output/'cases.txt').open('w', encoding='ascii') as cases, (output/'expected.txt').open('w', encoding='ascii') as expected:
        original = generate(args.sample, cases, expected)
    (output/'oracle-receipt.json').write_text(json.dumps(original, indent=2), 'utf-8')
    project = inputs[-1].read_text('utf-8-sig')
    includes = re.search(r'<AdditionalIncludeDirectories>(.*?)</AdditionalIncludeDirectories>', project)[1].split(';')
    project_dir = inputs[-1].parent
    includes = [(project_dir / p[len('$(ProjectDir)'):].replace('\\','/').lstrip('/')).resolve()
                for p in includes if p.startswith('$(ProjectDir)')]
    definitions = 'REGAMEDLL_ADD REGAMEDLL_API REGAMEDLL_FIXES REGAMEDLL_SSE REGAMEDLL_SELF REGAMEDLL_CHECKS UNICODE_FIXES BUILD_LATEST BUILD_LATEST_FIXES CLIENT_WEAPONS USE_QSTRING _CRT_SECURE_NO_WARNINGS NDEBUG'.split()
    flags = ['/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/utf-8',
             '/O2', '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/arch:AVX2',
             '/fp:precise', '/clang:-ffp-contract=off', '/Gy', '/Gw', *('/D'+x for x in definitions),
             *('/I'+str(x) for x in includes), '/I'+str(ROOT)]
    # A small generated test adapter compiles the actual upstream random helper
    # and table, not a second implementation. Never copy a complete source tree.
    util = inputs[-2].read_text('utf-8-sig')
    blocks = []
    for signature in ('unsigned int seed_table[256]', 'unsigned int U_Random()',
                      'void U_Srand(', 'float UTIL_SharedRandomFloat('):
        at = util.index(signature)
        end = util.index('\n}', at) + 2
        if signature.startswith('unsigned int seed_table'):
            end += 1
        blocks.append(util[at:end])
    random_adapter = output/'actual-random.cpp'
    random_adapter.write_text('// GPL-2.0-or-later; generated narrow adapter from actual ReGameDLL util.cpp\n'
                              '#include "precompiled.h"\nunsigned int glSeed;\n'+'\n'.join(blocks)+'\n', 'utf-8')
    executable = output/'cof-ballistics.exe'
    command = [str(args.compiler), *flags, str(inputs[2]), str(inputs[4]), str(random_adapter),
               '/Fe:'+str(executable), '/link', '/machine:x86', '/threads:1', '/opt:ref']
    environment = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    temporary = output/'temporary'
    temporary.mkdir()
    environment.update(TEMP=str(temporary), TMP=str(temporary))
    # Ask the actual compiler for each transitive include closure before the
    # build, rather than assuming a list of SDK headers or hashing a tree copy.
    dependencies = set(inputs) | {random_adapter, args.environment.resolve()}
    dependency_commands = []
    for index, source in enumerate((inputs[2], inputs[4], random_adapter)):
        depfile = output/f'headers-{index}.d'
        depcommand = [str(args.compiler), *flags, '/clang:-M', '/clang:-MF',
                      '/clang:'+str(depfile), '/clang:-MT', '/clang:adapter', str(source)]
        dependency_commands.append(depcommand)
        discovered = subprocess.run(depcommand, env=environment, cwd=output, capture_output=True,
                                    text=True, encoding='utf-8', errors='replace', timeout=60)
        (output/f'headers-{index}.log').write_text(discovered.stdout+discovered.stderr, 'utf-8')
        if discovered.returncode:
            raise RuntimeError('Actual compiler dependency discovery failed')
        make = depfile.read_text('utf-8').replace(chr(92)+chr(10), ' ')
        names = re.findall(r'(?:\\ |[^\s])+', make.split(':',1)[1])
        dependencies.update(Path(name.replace(chr(92)+' ', ' ')).resolve() for name in names)
    def dependency_identities():
        return {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(dependencies)}
    headers_before = dependency_identities()
    compiler_identity = dict(path=str(args.compiler.resolve()),
                             sha256=hashlib.sha256(args.compiler.read_bytes()).hexdigest())
    compiled = subprocess.run(command, env=environment, cwd=output, capture_output=True,
                              text=True, encoding='utf-8', errors='replace', timeout=120)
    (output/'compile.log').write_text(compiled.stdout+compiled.stderr, 'utf-8')
    run = None
    if compiled.returncode == 0:
        run = subprocess.run([str(executable), str(output/'cases.txt'), str(output/'expected.txt')],
                             cwd=output, capture_output=True, text=True, timeout=30)
        (output/'test.log').write_text(run.stdout+run.stderr, 'utf-8')
    negative = []
    if run is not None and run.returncode == 0:
        cases = (output/'cases.txt').read_text('ascii')
        expected = (output/'expected.txt').read_text('ascii')
        for name, case_text, expectation, wanted in (
            ('empty', '', expected, 6),
            ('truncated', cases.splitlines()[0].rsplit(' ',1)[0], expected, 3),
            ('wrong-result', cases, '00000000 '+expected.split(' ',1)[1], 5),
            ('extra-expected', cases, expected+'00000000\n', 7)):
            case_file, result_file = output/(name+'-cases.txt'), output/(name+'-expected.txt')
            case_file.write_text(case_text, 'ascii'); result_file.write_text(expectation, 'ascii')
            rejected = subprocess.run([str(executable), str(case_file), str(result_file)],
                                      cwd=output, capture_output=True, text=True, timeout=30)
            negative.append(dict(name=name, expected=wanted, actual=rejected.returncode,
                                 passed=rejected.returncode == wanted))
            (output/(name+'.log')).write_text(rejected.stdout+rejected.stderr, 'utf-8')
    after = identities()
    headers_after = dependency_identities()
    compiler_after = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    success = compiled.returncode == 0 and run is not None and run.returncode == 0 and before == after and headers_before == headers_after and compiler_identity['sha256'] == compiler_after and all(case['passed'] for case in negative)
    receipt = dict(command=command, dependencyCommands=dependency_commands, compiler=compiler_identity,
                   dependencyBefore=headers_before, dependencyAfter=headers_after, negative=negative,
                   original=original, compiled=compiled.returncode,
                   tested=run.returncode if run else None, before=before, after=after,
                   unchanged=before == after, sourceCopies=0, success=success,
                   boundary='Actual SDK types and adapter, controlled engine/entity edges; full scenario damage integration, deployment, gameplay and live performance remain unverified')
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print((run.stdout+run.stderr) if run else compiled.stdout+compiled.stderr)
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
