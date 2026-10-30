#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the real upgrade adapter against the current x86 SDK; one job only."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--clang', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--environment', type=Path, help='Optional JSON containing INCLUDE/LIB/LIBPATH; otherwise inherit the developer shell')
args = parser.parse_args()
OUT = args.output.absolute()
if not OUT.resolve().is_relative_to(ROOT / 'build'):
    parser.error('Output must remain under workspace build')
for node in (OUT, *OUT.parents):
    if node.is_symlink() or (hasattr(node, 'is_junction') and node.is_junction()):
        parser.error('Linked output refused')
OUT.mkdir(parents=True, exist_ok=False)
SDK = ROOT / 'external/ReGameDLL_CS/regamedll'
env = os.environ.copy()
if args.environment:
    environment = json.loads(args.environment.read_text(encoding='utf-8-sig'))
    env.update({key: value for key, value in environment.items() if key in ('INCLUDE', 'LIB', 'LIBPATH')})
compiler = args.clang.absolute()
linker = compiler.with_name('lld-link.exe')
sources = [
    ROOT / 'tests/csol-scenario-upgrade-xp/main.cpp',
    ROOT / 'tests/csol-scenario-entities/engine_stubs.cpp',
    ROOT / 'tests/csol-scenario-player-scan/player_stubs.cpp',
    ROOT / 'native/csol/server/scenario_player_upgrades.cpp',
    ROOT / 'native/csol/server/scenario_messages.cpp',
    ROOT / 'native/csol/scenario/upgrade_progress.cpp',
    ROOT / 'native/csol/scenario/upgrade_tables.cpp',
    ROOT / 'tests/csol-scenario-upgrade-xp/link_edges.cpp',
]
flags = ['/nologo', '/c', '/W4', '/utf-8', '/EHsc', '/MT', '/Gy', '/Gw', '/std:c++20',
    '--target=i686-pc-windows-msvc', '/arch:AVX2', '/O2', '/clang:-O3', '/clang:-flto=thin', '/fp:precise']
flags += ['/D' + define for define in ('REGAMEDLL_ADD', 'REGAMEDLL_API', 'REGAMEDLL_FIXES',
    'REGAMEDLL_SSE', 'REGAMEDLL_SELF', 'REGAMEDLL_CHECKS', 'UNICODE_FIXES', 'BUILD_LATEST',
    'BUILD_LATEST_FIXES', 'CLIENT_WEAPONS', 'USE_QSTRING', '_CRT_SECURE_NO_WARNINGS', 'NDEBUG', 'WIN32', '_WINDOWS')]
flags += ['/I' + str(SDK / path) for path in ('', 'hookers', 'common', 'dlls', 'engine',
    'public', 'public/regamedll', 'pm_shared', 'regamedll', 'testsuite')]

def identity(path):
    raw = path.read_bytes()
    return dict(path=str(path), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())

commands = []
objects = []
inputs = sources + [ROOT / 'native/csol/server/scenario_player_upgrades.h',
    ROOT / 'native/csol/server/scenario_messages.h', ROOT / 'native/csol/scenario/upgrade_progress.hpp',
    ROOT / 'native/csol/scenario/upgrade_tables.hpp',
    ROOT / 'native/csol/server/scenario_wall_rewards.h', ROOT / 'native/csol/server/scenario_breakable.h',
    ROOT / 'native/csol/server/game_rules_gate.h', ROOT / 'native/csol/server/damage_numbers.h',
    ROOT / 'native/csol/scenario/map_resources.hpp', ROOT / 'native/csol/scenario/config.hpp',
    ROOT / 'native/csol/scenario_abilities.hpp',
    SDK / 'regamedll/precompiled.h', SDK / 'dlls/player.h', SDK / 'dlls/cbase.h',
    SDK / 'dlls/ehandle.h', SDK / 'engine/edict.h', Path(__file__).resolve()]
before = [identity(path) for path in inputs]
for index, source in enumerate(sources):
    obj = OUT / f'{index}-{source.stem}.obj'
    command = [str(compiler), *flags, '/Fo' + str(obj), str(source)]
    with (OUT / f'{index}-compile.log').open('w', encoding='utf-8') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    commands.append(dict(command=command, returncode=result.returncode))
    if result.returncode:
        print((OUT / f'{index}-compile.log').read_text(encoding='utf-8'))
        raise SystemExit(result.returncode)
    objects.append(obj)
exe = OUT / 'upgrade-xp-tests.exe'
command = [str(linker), '/nologo', '/machine:X86', '/subsystem:console', '/incremental:no',
    '/OPT:REF', '/OPT:ICF', '/OPT:LLDLTO=3', '/opt:lldltojobs=1', '/out:' + str(exe), *map(str, objects)]
with (OUT / 'link.log').open('w', encoding='utf-8') as log:
    result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
commands.append(dict(command=command, returncode=result.returncode))
if result.returncode:
    print((OUT / 'link.log').read_text(encoding='utf-8'))
    raise SystemExit(result.returncode)
result = subprocess.run([str(exe)], cwd=ROOT, env=env, capture_output=True, text=True)
(OUT / 'test.log').write_text(result.stdout + result.stderr, encoding='utf-8')
after = [identity(path) for path in inputs]
report = dict(inputsBefore=before, inputsAfter=after, inputsUnchanged=before==after, compiler=identity(compiler),
    commands=commands, testReturncode=result.returncode, output=identity(exe),
    validation='Actual adapter/progress/message writer and SDK player layout; engine, character services and speed callback controlled.')
(OUT / 'tests.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(result.stdout + result.stderr)
raise SystemExit(result.returncode if before==after else 1)
