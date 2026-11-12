# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the actual SDK sender, compare complete original instruction output."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from oracle import generate

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sample', 'environment', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output.absolute()
    if output.exists() or not output.resolve().is_relative_to(ROOT/'build'):
        raise ValueError('Use a fresh workspace build directory')
    if any(p.is_symlink() or os.path.isjunction(p) for p in (output, *output.parents)):
        raise ValueError('Linked output directory')
    output.mkdir(parents=True)
    with (output/'cases.txt').open('w') as cases, (output/'expected.txt').open('w') as expected:
        original = generate(args.sample, cases, expected)
    sdk = ROOT/'external/ReGameDLL_CS/regamedll'
    project = sdk/'msvc/ReGameDLL.vcxproj'
    include_text = re.search(r'<AdditionalIncludeDirectories>(.*?)</AdditionalIncludeDirectories>', project.read_text('utf-8-sig'))[1]
    includes = [(project.parent/p[len('$(ProjectDir)'):].replace('\\', '/').lstrip('/')).resolve()
                for p in include_text.split(';') if p.startswith('$(ProjectDir)')]
    aborts = []
    inputs = {Path(__file__).resolve(), Path(__file__).with_name('oracle.py'), project, args.sample.resolve(), args.environment.resolve(),
              ROOT/'tests/cryoffear-sound-dispatch/oracle.py', ROOT/'tests/cryoffear-ballistics/oracle.py'}
    for filename, classes in (('cbase.h', ('CBaseEntity', 'CBaseDelay', 'CBaseAnimating', 'CBaseToggle')),
                              ('basemonster.h', ('CBaseMonster',)), ('player.h', ('CBasePlayer',))):
        path = sdk/'dlls'/filename; inputs.add(path); text = path.read_text('utf-8-sig')
        for cls in classes:
            start = re.search(r'class '+cls+r'\s*(?::[^{};]+)?\s*{', text).end()
            end, depth = start, 1
            while depth:
                depth += (text[end]=='{')-(text[end]=='}'); end += 1
            for sig in re.findall(r'virtual\s+([^;{}\n]+);', text[start:end-1]):
                sig = re.sub(r'\s*=\s*[^,)]+', '', sig)
                method = re.fullmatch(r'(.*?)([A-Za-z_][A-Za-z_0-9]*)(\s*\(.*)', re.sub(r'\bEXPORT\b', '', sig).strip())
                if not method:
                    raise ValueError('Unparsed actual SDK method')
                aborts.append(method[1]+cls+'::'+method[2]+method[3]+' { std::abort(); }')
    generated = output/'sdk-abort-virtuals.inc'
    generated.write_text('\n'.join(aborts), 'utf-8'); inputs.add(generated)
    sources = [ROOT/'tests/cryoffear-screen-shake/main.cpp', ROOT/'native/guest/cof/server/screen_shake.cpp']
    inputs.update(sources)
    config = json.loads((ROOT/'.tools/clang-toolchain.json').read_text('utf-8-sig'))
    compiler = Path(config['root'])/'bin/clang-cl.exe'; inputs.add(compiler)
    definitions = 'REGAMEDLL_ADD REGAMEDLL_API REGAMEDLL_FIXES REGAMEDLL_SSE REGAMEDLL_SELF REGAMEDLL_CHECKS UNICODE_FIXES BUILD_LATEST BUILD_LATEST_FIXES CLIENT_WEAPONS USE_QSTRING _CRT_SECURE_NO_WARNINGS NDEBUG'.split()
    flags = ['/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/utf-8', '/O2',
             '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/arch:AVX2', '/fp:precise',
             '/clang:-ffp-contract=off', '/Gy', '/Gw', *('/D'+d for d in definitions),
             *('/I'+str(p) for p in includes), '/I'+str(ROOT), '/I'+str(output)]
    environment = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    commands = []
    for i, source in enumerate(sources):
        dep = output/f'{i}.d'
        command = [str(compiler), *flags, '/clang:-M', '/clang:-MF', '/clang:'+str(dep), str(source)]
        commands.append(command)
        result = subprocess.run(command, cwd=output, env=environment, capture_output=True, text=True)
        (output/f'{i}-headers.log').write_text(result.stdout+result.stderr, 'utf-8')
        result.check_returncode()
        make = dep.read_text().replace(chr(92)+chr(10), ' ')
        inputs.update(Path(p.replace(chr(92)+' ', ' ')).resolve() for p in re.findall(r'(?:\\ |[^\s])+', make.split(':', 1)[1]))
    before = {str(p):sha(p) for p in sorted(inputs)}
    exe = output/'shake.exe'
    command = [str(compiler), *flags, *map(str, sources), '/Fe:'+str(exe), '/link', '/machine:x86', '/threads:1', '/opt:ref']
    result = subprocess.run(command, cwd=output, env=environment, capture_output=True, text=True)
    (output/'compile.log').write_text(result.stdout+result.stderr, 'utf-8'); result.check_returncode()
    result = subprocess.run([str(exe), str(output/'cases.txt'), str(output/'expected.txt')], cwd=output, capture_output=True, text=True)
    (output/'test.log').write_text(result.stdout+result.stderr, 'utf-8')
    after = {str(p):sha(p) for p in sorted(inputs)}
    receipt = dict(original=original, commands=commands+[command], before=before, after=after,
                   unchanged=before==after, test=result.returncode, success=not result.returncode and before==after,
                   sourceCopies=False, deployed=False, gameplay=False, performance=False)
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print(result.stdout+result.stderr)
    raise SystemExit(0 if receipt['success'] else 1)


if __name__ == '__main__':
    main()
