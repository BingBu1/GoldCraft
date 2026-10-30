#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the real weapon damage selector/JSON code and extracted SDK adapters.

Use a configured x86 developer shell, or pass --environment with an existing
JSON containing INCLUDE, LIB and LIBPATH. --validate-only checks all inputs and
adapter extraction without creating output, compiling, or running the tests.
This is an offline contract test, not a dedicated-server or gameplay test.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
SERVER = ROOT / 'native/csol/server'
SDK = ROOT / 'external/ReGameDLL_CS/regamedll'
RAPIDJSON = ROOT / 'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include'


def identity(path):
    raw = path.read_bytes()
    name = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path)
    return dict(path=name, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def extract_adapter(beam, cannon):
    """Keep production expressions verbatim, apart from newline normalization."""
    def one(pattern, source, name):
        matches = list(re.finditer(pattern, source, re.MULTILINE | re.DOTALL))
        if len(matches) != 1:
            raise ValueError(f'{name}: expected exactly one production adapter; review changed source')
        return matches[0].group()

    beam_function = one(r'^CSOLDamageMode CurrentDamageMode\(\)\n\{.*?^\}',
                        beam, 'beam CurrentDamageMode')
    cannon_mode = one(r'^    const int mode=int\(damageMode.value\);\n.*?normalDamage;',
                      cannon, 'cannon damage selection')
    for name, body in [('beam', beam_function), ('cannon', cannon_mode)]:
        if 'CSOL_CurrentWeaponDamageMode(legacy)' not in body:
            raise ValueError(f'{name}: automatic damage selector is absent')
    if 'const float base=' not in cannon_mode:
        raise ValueError('cannon: expected production base expression')
    adapter = '''#include "precompiled.h"
#include "weapon_damage_mode.h"
#include "weapon_damage.h"
bool g_bAllowedCSBot{};
namespace {
cvar_t damageMode{};
bool zombieMode{};
bool CSOL_ZombieMode(){return zombieMode;}
''' + beam_function + '''
}
CSOLDamageMode CSOL_TestBeamMode(float legacyCvar)
{damageMode.value=legacyCvar;return CurrentDamageMode();}
float CSOL_TestCannonBase(float legacyCvar,bool zombie,float normalDamage,float zombieDamage,float scenarioDamage,float escapeDamage)
{damageMode.value=legacyCvar;zombieMode=zombie;
''' + cannon_mode + '''
return base;}
'''
    fragments = {name: dict(bytes=len(body.encode('utf-8')),
                            sha256=hashlib.sha256(body.encode('utf-8')).hexdigest())
                 for name, body in [('beam', beam_function), ('cannon', cannon_mode)]}
    return adapter, fragments


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='New directory under workspace build')
    parser.add_argument('--compiler', '--clang', required=True, help='clang-cl executable path or PATH command')
    parser.add_argument('--environment', type=Path,
                        help='Optional SDK environment JSON; otherwise inherit the developer shell')
    parser.add_argument('--validate-only', action='store_true', help='Check parameters and source extraction only')
    args = parser.parse_args()
    try:
        output = args.output.absolute()
        if not output.resolve().is_relative_to(ROOT / 'build'):
            raise ValueError('Output must remain under workspace build')
        for node in (output, *output.parents):
            if node.is_symlink() or (hasattr(node, 'is_junction') and node.is_junction()):
                raise ValueError('Linked output is not allowed')
        if output.exists():
            raise ValueError('Use a new output directory to preserve previous evidence')
        output = output.resolve()
        found = shutil.which(args.compiler)
        if not found:
            raise ValueError('clang-cl was not found')
        compiler = Path(found).resolve()
        linker = compiler.with_name('lld-link.exe')
        if not linker.is_file():
            raise ValueError('Matching lld-link.exe was not found beside clang-cl')
        env = os.environ.copy()
        if args.environment:
            saved = json.loads(args.environment.read_text(encoding='utf-8-sig'))
            if not isinstance(saved, dict):
                raise ValueError('Compiler environment must be a JSON object')
            for name in ('INCLUDE', 'LIB', 'LIBPATH'):
                if name not in saved or not isinstance(saved[name], str):
                    raise ValueError(f'Compiler environment requires string {name}')
                env[name] = saved[name]
        includes = [SDK / part for part in ('', 'hookers', 'common', 'dlls', 'engine',
                    'public', 'public/regamedll', 'pm_shared', 'regamedll', 'testsuite')]
        includes += [RAPIDJSON, SERVER]
        for required in (SDK / 'regamedll/precompiled.h', RAPIDJSON / 'rapidjson/document.h'):
            if not required.is_file():
                raise ValueError(f'Missing project dependency: {required.relative_to(ROOT)}')
        compile_only = [SERVER / 'wpn_beamgun.cpp', SERVER / 'wpn_cannon.cpp']
        linked_sources = [SERVER / name for name in ('weapon_damage_mode.cpp', 'weapon_damage.cpp',
                          'weapon_damage_file.cpp', 'weapon_damage_mode_tests.cpp')]
        config = SERVER / 'config/beamgun_damage.json'
        inputs = [*compile_only, *linked_sources, config, Path(__file__).resolve(),
                  *[SERVER / name for name in ('weapon_damage_mode.h', 'weapon_damage.h',
                    'weapon_damage_file.h', 'game_rules_gate.h', 'wpn_beamgun.h')],
                  ROOT / 'native/csol/cannon.hpp', SDK / 'regamedll/precompiled.h',
                  RAPIDJSON / 'rapidjson/document.h']
        before = [identity(path) for path in inputs]
        beam, cannon = [path.read_text(encoding='utf-8-sig') for path in compile_only]
        adapter, fragments = extract_adapter(beam, cannon)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    if args.validate_only:
        print(json.dumps(dict(valid=True, output=str(output), sourceInputs=len(inputs),
                              adapterFragments=fragments, compiled=False, testsExecuted=False)))
        return

    output.mkdir(parents=True, exist_ok=False)
    adapter_path = output / 'sdk-adapters.cpp'
    adapter_path.write_text(adapter, encoding='utf-8')
    flags = ['/nologo', '/W4', '/utf-8', '/EHsc', '/MT', '/Gy', '/Gw', '/std:c++20',
             '--target=i686-pc-windows-msvc', '/arch:AVX2', '/O2', '/clang:-O3',
             '/clang:-flto=thin', '/fp:precise']
    flags += ['/D' + name for name in ('REGAMEDLL_ADD', 'REGAMEDLL_API', 'REGAMEDLL_FIXES',
              'REGAMEDLL_SSE', 'REGAMEDLL_SELF', 'REGAMEDLL_CHECKS', 'UNICODE_FIXES',
              'BUILD_LATEST', 'BUILD_LATEST_FIXES', 'CLIENT_WEAPONS', 'USE_QSTRING',
              '_CRT_SECURE_NO_WARNINGS', 'NDEBUG', 'WIN32', '_WINDOWS')]
    flags += ['/I' + str(path) for path in includes]
    commands, objects = [], []
    test_output, failure, assertions = '', None, None
    executable = output / 'weapon-mode-tests.exe'

    def run(label, command):
        result = subprocess.run(command, cwd=output, env=env, capture_output=True,
                                text=True, encoding='utf-8', errors='replace')
        log = result.stdout + result.stderr
        (output / (label + '.log')).write_text(log, encoding='utf-8')
        commands.append(dict(label=label, command=command, returncode=result.returncode))
        (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n', encoding='utf-8')
        print(f'{label}: {result.returncode}', flush=True)
        if result.returncode:
            raise RuntimeError(f'{label} failed ({result.returncode}); see {output / (label + ".log")}')
        return result

    try:
        for source in [*compile_only, *linked_sources, adapter_path]:
            obj = output / (source.stem + '.obj')
            run('compile-' + source.stem, [str(compiler), *flags, '/c', str(source), '/Fo' + str(obj)])
            if source not in compile_only:
                objects.append(obj)
        run('link', [str(compiler), '--target=i686-pc-windows-msvc', '/clang:-fuse-ld=lld', '/MT',
                     *map(str, objects), '/Fe' + str(executable), '/link', '/subsystem:console',
                     '/opt:ref', '/opt:icf', '/opt:lldltojobs=1', '/threads:1'])
        tested = run('test', [str(executable), str(config)])
        test_output = tested.stdout
        passed = re.search(r'^PASS (\d+) assertions:', test_output, re.MULTILINE)
        if not passed:
            raise RuntimeError('Test exited successfully without the expected assertion summary')
        assertions = int(passed[1])
    except (OSError, RuntimeError) as error:
        failure = str(error)
    after = [identity(path) for path in inputs]
    if before != after:
        failure = 'Source inputs changed during validation' + ('; ' + failure if failure else '')
    receipt = dict(passed=failure is None, failure=failure, inputsBefore=before, inputsAfter=after,
                   inputsUnchanged=before == after, compiler=identity(compiler), linker=identity(linker),
                   adapter=identity(adapter_path), adapterFragments=fragments, commands=commands,
                   assertions=assertions, executable=identity(executable) if executable.is_file() else None,
                   boundary='Offline rules provider; real mode selector, beam JSON parser/loader and SDK adapters extracted from production. '
                            'Full beam/cannon translation units compile but are not linked into the fixture. '
                            'Cannon profile values are fixture parameters. No deployment or gameplay.')
    (output / 'evidence.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    if failure:
        raise SystemExit(failure)
    print(test_output, end='')


if __name__ == '__main__':
    main()
