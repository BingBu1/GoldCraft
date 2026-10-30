#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile complete session, inventory and weapon HUD/prediction production TUs."""
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
    parser.add_argument('--session', type=Path, default=Path('native/csol/client/session.cpp'))
    parser.add_argument('--expect-failure', action='store_true')
    parser.add_argument('--full-client', action='store_true')
    args = parser.parse_args()
    output = args.output.absolute()
    session = args.session.absolute()
    if not output.resolve().is_relative_to(ROOT / 'build') or not session.resolve().is_relative_to(ROOT):
        parser.error('Inputs and output must remain inside workspace; output under build')
    for node in (output, *output.parents):
        if node.is_symlink() or (hasattr(node, 'is_junction') and node.is_junction()):
            parser.error('Linked output refused')
    output.mkdir(parents=True, exist_ok=False)
    fixture = ROOT / 'tests/csol-weapon-slots/client.cpp'
    production = [ROOT / ('native/csol/' + path) for path in [
        'client/weapons.cpp', 'client/weapon_inventory.cpp', 'client/crosshair.cpp',
        'client/pickup_history.cpp', 'client/beamgun.cpp', 'client/cannon.cpp', 'beamgun_state.cpp', 'weapon_slots.cpp']]
    sources = [fixture, ROOT / 'tests/csol-weapon-slots/session_edges.cpp', *production]
    headers = sorted((ROOT / 'native/csol/client').glob('*.hpp'))
    inputs = [session, *sources, ROOT / 'tests/csol-weapon-slots/session_clock.hpp',
              ROOT / 'tests/csol-weapon-slots/wide_ammo.hpp', ROOT / 'tests/csol-weapon-slots/beam_render.hpp',
              *headers, Path(__file__).resolve()]
    before = [identity(path) for path in inputs]
    snapshot_dir = output / 'frozen-session'
    snapshot_dir.mkdir()
    # Preserve the TU's local-header lookup precedence: root protocol headers
    # share names with client headers and cannot stand in for their declarations.
    for header in headers:
        (snapshot_dir / header.name).write_bytes(header.read_bytes())
    actual = snapshot_dir / 'session.cpp'
    actual.write_bytes(session.read_bytes())
    sdk = ROOT / 'external/MetaHookSv-20261007/MetaHook/include'
    includes = [ROOT / 'native/csol', ROOT / 'native/csol/client', sdk, sdk / 'Interface',
                *(sdk / 'HLSDK' / name for name in ['cl_dll', 'common', 'engine', 'pm_shared', 'public']),
                sdk / 'SourceSDK', sdk / 'SourceSDK/tier0', sdk / 'SourceSDK/tier1',
                ROOT / 'external/MetaHookSv-20261007/thirdparty/glew_fork/include']
    flags = ['--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT',
             '/W3', '/utf-8', '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld',
             '/arch:AVX2', '/fp:precise', '/DNDEBUG', '/DNOMINMAX', '/DNO_MALLOC_OVERRIDE',
             '/DGLEW_STATIC', '/DGLEW_NO_GLU', *('/I' + str(path) for path in includes)]
    executable = output / 'session-clock.exe'
    command = [str(args.clang), *flags, str(actual), *(str(path) for path in sources),
               '/Fe' + str(executable), '/Fo' + str(output) + '/', '/link', '/MACHINE:X86']
    (output / 'command.json').write_text(json.dumps(command, indent=2), encoding='utf-8')
    compiled = subprocess.run(command, cwd=output, capture_output=True, text=True, encoding='utf-8', errors='replace')
    (output / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
    result = None
    full_result = None
    if not compiled.returncode:
        result = subprocess.run([str(executable), '--session-clock-only'], cwd=output,
                                capture_output=True, text=True, encoding='utf-8', errors='replace')
        (output / 'test.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        if args.full_client and result.returncode == 0:
            full_result = subprocess.run([str(executable)], cwd=output,
                                         capture_output=True, text=True, encoding='utf-8', errors='replace')
            (output / 'full-client.log').write_text(full_result.stdout + full_result.stderr, encoding='utf-8')
    after = [identity(path) for path in inputs]
    receipt = dict(at=datetime.datetime.now(datetime.timezone.utc).isoformat(), inputsBefore=before, inputsAfter=after,
                   inputsUnchanged=before == after, compileExit=compiled.returncode,
                   testExit=None if result is None else result.returncode, expectedFailure=args.expect_failure,
                   fullClientRequested=args.full_client,
                   fullClientExit=None if full_result is None else full_result.returncode,
                   fullClientOutput='' if full_result is None else full_result.stdout + full_result.stderr,
                   snapshot=identity(actual), output='' if result is None else result.stdout + result.stderr,
                   boundary='Complete production session/inventory/weapons/pickup/crosshair/beamgun/cannon/weapon protocol TUs, matching SDK. '
                            'Controlled engine clock/level/messages/native HUD callbacks; unrelated scenario UI resets stubbed. '
                            'Synthetic clock samples demonstrate the candidate chain, not proof these exact corrections occurred in B.')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    if result:
        print(result.stdout.splitlines()[-1] if result.stdout else 'no test stdout')
        print(result.stderr, end='')
    if full_result:
        print(full_result.stdout + full_result.stderr, end='')
    full_failed = args.full_client and (full_result is None or full_result.returncode != 0)
    if compiled.returncode or result is None or bool(result.returncode) != args.expect_failure or before != after or full_failed:
        raise RuntimeError('Unexpected result or changed inputs; see ' + str(output / 'receipt.json'))


if __name__ == '__main__':
    main()
