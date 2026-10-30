#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the complete production weapon inventory with actual SDK interfaces."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def identity(path):
    return dict(path=path.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--inventory', type=Path, default=Path('native/csol/client/weapon_inventory.cpp'))
    parser.add_argument('--expect-failure', action='store_true')
    args = parser.parse_args()
    output = args.output.absolute()
    inventory = args.inventory.absolute()
    if not output.resolve().is_relative_to(ROOT / 'build') or not inventory.resolve().is_relative_to(ROOT):
        parser.error('Inputs and output must remain inside workspace; output under build')
    for node in (output, *output.parents):
        if node.is_symlink() or (hasattr(node, 'is_junction') and node.is_junction()):
            parser.error('Linked output refused')
    output.mkdir(parents=True, exist_ok=False)
    fixture = ROOT / 'tests/csol-weapon-slots/menu.cpp'
    protocol = ROOT / 'native/csol/weapon_slots.cpp'
    inputs = [inventory, fixture, protocol, ROOT / 'native/csol/weapon_slots.hpp',
              ROOT / 'native/csol/client/weapon_inventory.hpp', ROOT / 'native/csol/client/runtime.hpp', Path(__file__).resolve()]
    before = [identity(path) for path in inputs]
    # A byte-identical independent copy freezes the entire production TU for this run.
    actual = output / 'weapon_inventory.cpp'
    actual.write_bytes(inventory.read_bytes())
    sdk = ROOT / 'external/MetaHookSv-20261007/MetaHook/include'
    includes = [ROOT / 'native/csol', ROOT / 'native/csol/client', sdk, sdk / 'Interface',
                *(sdk / 'HLSDK' / name for name in ['cl_dll', 'common', 'engine', 'pm_shared', 'public']),
                sdk / 'SourceSDK', sdk / 'SourceSDK/tier0', sdk / 'SourceSDK/tier1',
                ROOT / 'external/MetaHookSv-20261007/thirdparty/glew_fork/include']
    flags = ['--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT',
             '/W3', '/utf-8', '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld',
             '/arch:AVX2', '/fp:precise', '/DNDEBUG', '/DNOMINMAX', '/DNO_MALLOC_OVERRIDE',
             '/DGLEW_STATIC', '/DGLEW_NO_GLU', *('/I' + str(path) for path in includes)]
    executable = output / 'weapon-menu.exe'
    command = [str(args.clang), *flags, str(fixture), str(actual), str(protocol),
               '/Fe' + str(executable), '/Fo' + str(output) + '/', '/link', '/MACHINE:X86']
    (output / 'command.json').write_text(json.dumps(command, indent=2), encoding='utf-8')
    compiled = subprocess.run(command, cwd=output, capture_output=True, text=True, encoding='utf-8', errors='replace')
    (output / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
    result = None
    if not compiled.returncode:
        result = subprocess.run([str(executable)], cwd=output, capture_output=True, text=True, encoding='utf-8', errors='replace')
        (output / 'test.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    after = [identity(path) for path in inputs]
    receipt = dict(at=datetime.datetime.now(datetime.timezone.utc).isoformat(), inputsBefore=before, inputsAfter=after,
                   inputsUnchanged=before == after, compileExit=compiled.returncode,
                   testExit=None if result is None else result.returncode, expectedFailure=args.expect_failure,
                   snapshot=identity(actual), output='' if result is None else result.stdout + result.stderr,
                   boundary='Whole production weapon_inventory.cpp and weapon_slots.cpp with matching SDK headers. '
                            'Controlled engine command/message/time/graphics edges; no native HUD binary, full plugin chain or gameplay.')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    if result:
        print(result.stdout + result.stderr, end='')
    if compiled.returncode or result is None or bool(result.returncode) != args.expect_failure or before != after:
        raise RuntimeError('Unexpected result or changed inputs; see ' + str(output / 'receipt.json'))


if __name__ == '__main__':
    main()
