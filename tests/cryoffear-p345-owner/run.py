# SPDX-License-Identifier: GPL-3.0-or-later
"""Check P345's canonical native owner view with actual SDK and player models."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / 'external/ReGameDLL_CS/regamedll'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def body(path, signature, only=False):
    text = path.read_text('utf-8-sig'); start = text.index(signature)
    at = text.index('{', start); end = at + 1; depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[at + 1:end - 1] if only else text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('compiler', 'environment', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument("--weapon",choices=("p345","revolver"),default="p345")
    args = parser.parse_args(); output = args.output.resolve()
    if output.exists() or not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build directory')
    for path in (args.output.absolute(), *args.output.absolute().parents):
        if path.is_symlink() or os.path.isjunction(path):
            raise ValueError('Linked output directory rejected')
    output.mkdir(parents=True)
    canonical_fixture=ROOT/'tests/cryoffear-owner-runtime/main.cpp'
    prefix=canonical_fixture.read_text('utf-8-sig').split('int main(int argc, char** argv)',1)[0]
    prefix=prefix.replace('../cryoffear-glock-weapon/main.cpp','tests/cryoffear-glock-weapon/main.cpp')
    (output/'shared-owner-runtime-sdk.inc').write_text(prefix,'utf-8')
    model_runner = ROOT / 'tests/cryoffear-player-animation/run.py'
    model_specs = runpy.run_path(str(model_runner))['MODELS']
    model_root = ROOT / 'sandbox/cs-server/Half-Life/cstrike/models/player'
    models = [model_root / name / (name + '.mdl') for name, _ in model_specs]
    for model, (_, expected) in zip(models, model_specs):
        if sha(model) != expected:
            raise ValueError('Actual model identity changed: ' + str(model))
    sources = [ROOT / ('tests/cryoffear-'+args.weapon+'-owner/main.cpp'),
        ROOT / 'native/csol/server/weapons/guest_cof_owner_runtime.cpp',
        ROOT / ('native/csol/server/weapons/guest_cof_'+args.weapon+'_owner.cpp'),
        ROOT / 'native/csol/server/weapons/guest_cof_owner.cpp',
        ROOT / 'native/guest/cof/server/owner_state.cpp',
        ROOT / 'native/guest/cof/server/glock_weapon.cpp',
        ROOT / 'native/guest/cof/magazine_actions.cpp',
        ROOT / 'native/guest/cof/magazine_reload.cpp', ROOT / 'native/guest/cof/spread.cpp',
        ROOT / 'native/guest/cof/server/player_animation.cpp', ROOT / 'native/guest/cof/player_actions.cpp']
    if args.weapon=='revolver':sources.append(ROOT/'native/guest/cof/server/revolver_weapon.cpp')
    inputs = set(sources) | set(models) | {Path(__file__).resolve(), model_runner,
        args.environment.resolve(), SDK / 'msvc/ReGameDLL.vcxproj',canonical_fixture}
    for name, path, sig, only in (
        ('sdk-sequence-info.inc', SDK / 'dlls/animation.cpp', 'void GetSequenceInfo(', False),
        ('sdk-sequence-flags.inc', SDK / 'dlls/animation.cpp', 'int GetSequenceFlags(', False),
        ('sdk-reset-sequence.inc', SDK / 'dlls/animating.cpp', 'void EXT_FUNC CBaseAnimating::__API_HOOK(ResetSequenceInfo)()', True)):
        inputs.add(path); (output / name).write_text(body(path, sig, only), 'utf-8')
    lifecycle = []
    for path, sig in (
        (SDK / 'dlls/weapons.cpp', 'void CBasePlayerWeapon::Spawn()'),
        (SDK / 'dlls/weapons.cpp', 'void CBasePlayerItem::AttachToPlayer('),
        (SDK / 'dlls/weapons.cpp', 'int CBasePlayerWeapon::AddToPlayer('),
        (SDK / 'dlls/weapons.cpp', 'int CBasePlayerItem::AddToPlayer('),
        (SDK / 'dlls/weapons.cpp', 'int CBasePlayerWeapon::AddDuplicate('),
        (SDK / 'dlls/player.cpp', 'int CBasePlayer::GetAmmoIndex('),
        (SDK / 'dlls/player.cpp', 'bool CBasePlayer::HasPlayerItem('),
        (SDK / 'dlls/weapons.cpp', 'const char *CBasePlayerItem::pszAmmo1('),
        (SDK / 'dlls/weapons.cpp', 'const char *CBasePlayerItem::pszAmmo2('),
        (SDK / 'dlls/API/CSPlayerItem.cpp', 'EXT_FUNC void CCSPlayerItem::SetItemInfo('),
        (SDK / 'dlls/API/CSPlayerItem.cpp', 'EXT_FUNC int CCSPlayerItem::GetItemInfo('),
        (SDK / 'dlls/util.cpp', 'void UTIL_SetOrigin('),
        (SDK / 'dlls/util.cpp', 'void UTIL_SetSize(')):
        inputs.add(path); lifecycle.append(body(path, sig).replace('m_ItemInfoEx', 'CSPlayerItem()->m_ItemInfo'))
    (output / 'sdk-world-lifecycle.inc').write_text('\n'.join(lifecycle), 'utf-8')
    aborts = []
    for header, classes in (
        ('cbase.h', ('CBaseEntity', 'CBaseDelay', 'CBaseAnimating', 'CBaseToggle')),
        ('basemonster.h', ('CBaseMonster',)), ('player.h', ('CBasePlayer',)),
        ('weapons.h', ('CBasePlayerItem', 'CBasePlayerWeapon'))):
        path = SDK / 'dlls' / header; inputs.add(path); text = path.read_text('utf-8-sig')
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
    replaced = ('void CBasePlayerWeapon::Spawn(', 'void CBasePlayerItem::AttachToPlayer(',
                'int CBasePlayerWeapon::AddToPlayer(', 'int CBasePlayerItem::AddToPlayer(',
                'int CBasePlayerWeapon::AddDuplicate(')
    (output / 'sdk-abort-virtuals.inc').write_text('\n'.join(p for p in aborts if not p.startswith(replaced)), 'utf-8')
    project = SDK / 'msvc/ReGameDLL.vcxproj'
    includes = re.search(r'<AdditionalIncludeDirectories>(.*?)</AdditionalIncludeDirectories>',
                         project.read_text('utf-8-sig'))[1].split(';')
    includes = [(project.parent / p[len('$(ProjectDir)'):].replace('\\', '/').lstrip('/')).resolve()
                for p in includes if p.startswith('$(ProjectDir)')]
    definitions = ('REGAMEDLL_ADD REGAMEDLL_API REGAMEDLL_FIXES REGAMEDLL_SSE REGAMEDLL_SELF '
                   'REGAMEDLL_CHECKS UNICODE_FIXES BUILD_LATEST BUILD_LATEST_FIXES CLIENT_WEAPONS '
                   'USE_QSTRING _CRT_SECURE_NO_WARNINGS NDEBUG').split()
    flags = ['/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/utf-8',
        '/O2', '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/arch:AVX2', '/fp:precise',
        '/clang:-ffp-contract=off', '/Gy', '/Gw', *('/D' + d for d in definitions),
        *('/I' + str(p) for p in includes), '/I' + str(ROOT), '/I' + str(output),
        '/I'+str(ROOT/'native/include'), '/I'+str(ROOT/'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include')]
    environment = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    temp = output / 'temp'; temp.mkdir(); environment.update(TEMP=str(temp), TMP=str(temp))
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
    before = {str(p): sha(p) for p in sorted(inputs)}; compiler_hash = sha(args.compiler)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    executable = output / 'owner-runtime.exe'
    command = [str(args.compiler), *flags, *map(str, sources), '/Fe:' + str(executable),
               '/link', '/machine:x86', '/threads:1', '/opt:ref']
    compiled = subprocess.run(command, cwd=output, env=environment, capture_output=True, text=True, timeout=120)
    (output / 'compile.log').write_text(compiled.stdout + compiled.stderr, 'utf-8')
    tested = None
    if not compiled.returncode:
        tested = subprocess.run([str(executable), *map(str, models)], cwd=output,
                                capture_output=True, text=True, timeout=30)
        (output / 'test.log').write_text(tested.stdout + tested.stderr, 'utf-8')
    after = {str(p): sha(p) for p in sorted(inputs)}
    success = (not compiled.returncode and tested is not None and not tested.returncode and
               before == after and compiler_hash == sha(args.compiler))
    receipt = dict(success=success, revision=revision, command=command, dependencyCommands=dependency_commands,
        compiled=compiled.returncode, tested=tested.returncode if tested else None,
        before=before, after=after, compilerSha256=compiler_hash, unchanged=before == after, sourceCopies=0,
        boundary='Actual P345 canonical owner view, native inventory frame capability and actual models; controlled authority/effects; no external production hooks, whole DLL, deployment, gameplay or live performance acceptance')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print((tested.stdout + tested.stderr) if tested else compiled.stdout + compiled.stderr)
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
