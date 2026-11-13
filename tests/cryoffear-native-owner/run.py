# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile native CS owner adaptation against current actual SDK declarations."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / 'external/ReGameDLL_CS/regamedll'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('compiler', 'environment', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build directory')
    for path in (args.output.absolute(), *args.output.absolute().parents):
        if path.is_symlink() or os.path.isjunction(path):
            raise ValueError('Linked build directory rejected')
    output.mkdir(parents=True)
    sources = [ROOT / 'tests/cryoffear-native-owner/main.cpp',
               ROOT / 'native/csol/server/weapons/guest_cof_owner.cpp',
               ROOT / 'native/guest/cof/server/owner_state.cpp']
    inputs = set(sources) | {Path(__file__).resolve(), args.environment.resolve(),
        ROOT / 'native/csol/server/weapons/guest_cof_owner.hpp',
        SDK / 'dlls/player.cpp', SDK / 'pm_shared/pm_shared.cpp', SDK / 'msvc/ReGameDLL.vcxproj'}
    text = (SDK / 'dlls/player.cpp').read_text('utf-8-sig')
    start = text.index('void EXT_FUNC CBasePlayer::__API_HOOK(Jump)()')
    at = text.index('{', start)
    # Stop at the real accepted-jump hook. The fixture exercises the gates
    # separately from AcceptedNativeJump and must not copy the host callback.
    end = text.index('CSOL_GuestCofAcceptedJump(*this);', at)
    gates = text[at + 1:end].replace('return;', 'return false;')
    # Only the actual SDK gate block is adapted; an integration hook must still
    # call AcceptedNativeJump at the actual accepted branch in production.
    (output / 'sdk-jump-gates.inc').write_text(
        'bool SdkJumpAccepts(CBasePlayer& player) {\n'
        'auto* pev = player.pev;\n'
        'const int m_afButtonPressed = player.m_afButtonPressed;\n' + gates +
        '\nreturn true;\n}\n', 'utf-8')
    aborts = []
    for header, classes in (
        ('cbase.h', ('CBaseEntity', 'CBaseDelay', 'CBaseAnimating', 'CBaseToggle')),
        ('basemonster.h', ('CBaseMonster',)), ('player.h', ('CBasePlayer',)),
        ('weapons.h', ('CBasePlayerItem', 'CBasePlayerWeapon'))):
        path = SDK / 'dlls' / header
        inputs.add(path)
        text = path.read_text('utf-8-sig')
        for cls in classes:
            match = re.search(r'class ' + cls + r'\s*(?::[^{};]+)?\s*{', text)
            at = match.end(); end = at; depth = 1
            while depth:
                depth += (text[end] == '{') - (text[end] == '}'); end += 1
            for sig in re.findall(r'virtual\s+([^;{}\n]+);', text[at:end - 1]):
                sig = re.sub(r'\s*=\s*[^,)]+', '', sig)
                sig = re.sub(r'\bEXPORT\b', '', sig).strip()
                match = re.fullmatch(r'(.*?)([A-Za-z_][A-Za-z_0-9]*)(\s*\(.*)', sig)
                if not match:
                    raise ValueError('Unparsed SDK virtual ' + sig)
                aborts.append(match[1] + cls + '::' + match[2] + match[3] + ' { std::abort(); }')
    (output / 'sdk-abort-virtuals.inc').write_text('\n'.join(aborts), 'utf-8')
    project = SDK / 'msvc/ReGameDLL.vcxproj'
    includes = re.search(r'<AdditionalIncludeDirectories>(.*?)</AdditionalIncludeDirectories>',
                         project.read_text('utf-8-sig'))[1].split(';')
    includes = [(project.parent / p[len('$(ProjectDir)'):].replace('\\', '/').lstrip('/')).resolve()
                for p in includes if p.startswith('$(ProjectDir)')]
    definitions = ('REGAMEDLL_ADD REGAMEDLL_API REGAMEDLL_FIXES REGAMEDLL_SSE REGAMEDLL_SELF '
                   'REGAMEDLL_CHECKS UNICODE_FIXES BUILD_LATEST BUILD_LATEST_FIXES CLIENT_WEAPONS '
                   'USE_QSTRING _CRT_SECURE_NO_WARNINGS NDEBUG').split()
    flags = ['/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/utf-8',
             '/O2', '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/arch:AVX2',
             '/fp:precise', '/clang:-ffp-contract=off', '/Gy', '/Gw',
             *('/D' + d for d in definitions), *('/I' + str(p) for p in includes),
             '/I' + str(ROOT), '/I' + str(output),
             '/I'+str(ROOT/'native/include'), '/I'+str(ROOT/'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include')]
    environment = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    temp = output / 'temp'; temp.mkdir()
    environment.update(TEMP=str(temp), TMP=str(temp))
    dependency_commands = []
    for index, source in enumerate(sources):
        dep = output / f'{index}.d'
        command = [str(args.compiler), *flags, '/clang:-M', '/clang:-MF', '/clang:' + str(dep), str(source)]
        dependency_commands.append(command)
        run = subprocess.run(command, cwd=output, env=environment, capture_output=True, text=True, timeout=60)
        (output / f'{index}-headers.log').write_text(run.stdout + run.stderr, 'utf-8')
        if run.returncode:
            raise RuntimeError('Dependency discovery failed; see headers log')
        make = dep.read_text().replace(chr(92) + chr(10), ' ')
        inputs.update(Path(p.replace(chr(92) + ' ', ' ')).resolve()
                      for p in re.findall(r'(?:\\ |[^\s])+', make.split(':', 1)[1]))
    inputs.update(output.glob('*.inc'))
    before = {str(p): sha(p) for p in sorted(inputs)}
    revision = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=ROOT, capture_output=True,
                              text=True, check=True).stdout.strip()
    executable = output / 'native-owner.exe'
    command = [str(args.compiler), *flags, *map(str, sources), '/Fe:' + str(executable),
               '/link', '/machine:x86', '/threads:1', '/opt:ref']
    compiled = subprocess.run(command, cwd=output, env=environment, capture_output=True, text=True, timeout=120)
    (output / 'compile.log').write_text(compiled.stdout + compiled.stderr, 'utf-8')
    tested = None
    if not compiled.returncode:
        tested = subprocess.run([str(executable)], cwd=output, capture_output=True, text=True, timeout=30)
        (output / 'test.log').write_text(tested.stdout + tested.stderr, 'utf-8')
    after = {str(p): sha(p) for p in sorted(inputs)}
    success = not compiled.returncode and tested is not None and not tested.returncode and before == after
    receipt = dict(success=success, revision=revision, command=command, dependencyCommands=dependency_commands,
        compiled=compiled.returncode, tested=tested.returncode if tested else None,
        before=before, after=after, unchanged=before == after, sourceCopies=0,
        boundary='Native owner actual SDK declaration/gate tests; authority is controlled; no full PM physics, original CoF movement, whole DLL, deployment, gameplay or live performance acceptance')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print((tested.stdout + tested.stderr) if tested else compiled.stdout + compiled.stderr)
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
