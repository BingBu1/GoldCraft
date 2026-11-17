#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build production manifest senders, codecs and fragment queue with real SDK types.

Only named function adapters are generated. No production source tree is copied.
The previous sender is pinned to a Git revision for an independent wire baseline.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def function(data, name):
    match = re.search(rb'^[^\r\n;{}]*\b' + name.encode() + rb'\([^\r\n]*\)\s*\{', data, re.M)
    if not match:
        raise ValueError('Function not found: ' + name)
    start = match.start()
    cursor = data.index(b'{', match.start())
    depth, state = 0, 'code'
    while cursor < len(data):
        value, pair = data[cursor:cursor + 1], data[cursor:cursor + 2]
        if state == 'line':
            if value == b'\n': state = 'code'
        elif state == 'comment':
            if pair == b'*/': state = 'code'; cursor += 1
        elif state in ('"', "'"):
            if value == b'\\': cursor += 1
            elif value == state.encode(): state = 'code'
        elif pair in (b'//', b'/*'):
            state = 'line' if pair == b'//' else 'comment'; cursor += 1
        elif value in (b'"', b"'"): state = value.decode()
        elif value == b'{': depth += 1
        elif value == b'}':
            depth -= 1
            if not depth: return start, cursor + 1
        cursor += 1
    raise ValueError('Unterminated function: ' + name)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--environment', type=Path, required=True)
    p.add_argument('--compiler', type=Path, required=True, help='Matching x86-capable Clang compiler')
    p.add_argument('--baseline-ref', default='d7b6f094e8d547635e4b297e0be61118a78e5e39')
    p.add_argument('--expect-former', action='store_true', help='Require the old sender to fail the coalescing regression')
    a = p.parse_args(); out = a.out.resolve()
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Fresh workspace build output required')
    if any(v.is_symlink() or v.is_junction() for v in (out, *out.parents)):
        raise ValueError('Linked output path')
    out.mkdir(parents=True)
    sdk = ROOT / 'external/ReHLDS/rehlds'
    spec = importlib.util.spec_from_file_location('studio_input_guard', ROOT / 'tests/engine-studio-storage/run.py')
    guard = importlib.util.module_from_spec(spec); spec.loader.exec_module(guard)
    inputs = guard.Inputs([ROOT])
    ranges = []
    bit_source = inputs.add(sdk / 'engine/common.cpp')
    bit_data = bit_source.read_bytes()
    bit_start = bit_data.index(b'typedef struct bf_write_s')
    bit_end = bit_data.index(b'} bf_write_t;', bit_start) + len(b'} bf_write_t;')
    (out / 'bit-storage-under-test.inc').write_bytes(bit_data[bit_start:bit_end])
    ranges.append(dict(source=identity(bit_source), name='bf_write_t', start=bit_start, end=bit_end,
                       sha256=hashlib.sha256(bit_data[bit_start:bit_end]).hexdigest()))

    def extract(destination, units):
        parts = []
        for path, names in units:
            source = inputs.add(ROOT / path); data = source.read_bytes()
            for name in names:
                start, end = function(data, name); parts.append(data[start:end])
                ranges.append(dict(source=identity(source), name=name, start=start, end=end,
                                   sha256=hashlib.sha256(data[start:end]).hexdigest()))
        (out / destination).write_bytes(b'\n'.join(parts) + b'\n')

    extract('wire-under-test.inc', [
        ('external/ReHLDS/rehlds/engine/common.cpp', [
            'SZ_Clear', 'SZ_HasSpace', 'SZ_GetSpace', 'SZ_Write', 'MSG_WriteByte', 'MSG_WriteLong',
            'MSG_WriteString', 'MSG_WBits_MaybeFlush', 'MSG_WriteBits', 'MSG_StartBitWriting',
            'MSG_EndBitWriting', 'MSG_WriteBitString', 'MSG_WriteBitData']),
        ('external/ReHLDS/rehlds/engine/sv_user.cpp', ['SV_ShouldSendConsistencyList', 'SV_SendConsistencyList'])])
    extract('netchan-under-test.inc', [('external/ReHLDS/rehlds/engine/net_chan.cpp', [
        'Netchan_AllocFragbuf', 'Netchan_AddFragbufToTail', 'Netchan_CreateFragments_',
        'Netchan_CreateFragments', 'Netchan_FragSend'])])
    extract('sender-under-test.inc', [
        ('native/engine/precache.cpp', ['GC_SendResourceManifest', 'GC_WriteResourceChunk']),
        ('external/ReHLDS/rehlds/engine/sv_main.cpp', ['SV_SendResources', 'SV_SendResources_internal'])])
    revision = subprocess.check_output(['git', 'rev-parse', a.baseline_ref + '^{commit}'], cwd=ROOT, text=True).strip()
    old = subprocess.check_output(['git', 'show', revision + ':native/engine/precache.cpp'], cwd=ROOT)
    start, end = function(old, 'GC_SendResourceManifest')
    (out / 'legacy-sender-under-test.inc').write_bytes(
        old[start:end].replace(b'void GC_SendResourceManifest(', b'void Legacy_SendResourceManifest(', 1))
    ranges.append(dict(gitRevision=revision, source='native/engine/precache.cpp', name='GC_SendResourceManifest',
                       start=start, end=end, sha256=hashlib.sha256(old[start:end]).hexdigest()))
    sources = [inputs.add(HERE / 'server.cpp'), inputs.add(sdk / 'rehlds/hookchains_impl.cpp')]
    inputs.add(Path(__file__))
    includes = [out, ROOT / 'native/engine', ROOT / 'native/include']
    includes += [sdk / v for v in ('', 'metamod/include', 'public/rehlds', 'common', 'engine',
                                  'public', 'pm_shared', 'rehlds', 'testsuite')]
    includes += [sdk.parent / 'dep/bzip2/include', sdk.parent / 'dep/cppunitlite/include']
    defines = ['REHLDS_API', 'REHLDS_FIXES', 'REHLDS_SSE', 'REHLDS_OPT_PEDANTIC', 'REHLDS_SELF',
               'REHLDS_CHECKS', 'DEDICATED', 'SWDS', 'NDEBUG', 'NOMINMAX', '_CRT_SECURE_NO_WARNINGS']
    if a.expect_former: defines.append('VERIFY_FORMER_SENDER')
    flags = ['--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT', '/utf-8',
             '/O2', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/Gy', '/Gw', '/fp:precise',
             '/clang:-ffp-contract=off', '/Y-', '/showIncludes', *['/D' + d for d in defines],
             *['/I' + str(v) for v in includes]]
    commands, objects = [], []
    for i, source in enumerate(sources):
        obj = out / ('unit-' + str(i) + '.obj'); objects.append(str(obj))
        commands.append(dict(name='compile-' + str(i), argv=[str(a.compiler), *flags, '/c', str(source), '/Fo' + str(obj)]))
    library = ROOT / 'build/rehlds/Release/bzip2.lib'
    executable = out / 'server.exe'
    commands.append(dict(name='link', argv=[str(a.compiler), '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT',
        *objects, str(library), '/Fe' + str(executable), '/link', '/OPT:REF', '/OPT:ICF', '/OPT:LLDLTO=3', '/threads:1']))
    commands.append(dict(name='test', argv=[str(executable), str(out / 'wire')]))
    for path in sdk.rglob('*'):
        if path.is_file() and path.suffix.lower() in ('.h', '.hpp', '.inl', '.inc'):
            inputs.records[str(path.resolve())] = identity(path)
    tracked = [*inputs.records.values(), *[identity(v) for v in out.glob('*.inc')],
               identity(a.compiler), identity(library), identity(a.environment)]
    (out / 'plan.json').write_text(json.dumps(dict(commands=commands, sources=tracked, extractions=ranges,
        baselineRevision=revision, sourceCopies=0, liveGame=False), indent=2), 'utf8')
    environment = dict(os.environ)
    for key, value in json.loads(a.environment.read_text('utf-8-sig')).items():
        for oldkey in list(environment):
            if oldkey.casefold() == key.casefold(): del environment[oldkey]
        environment[key] = value
    environment['VSLANG'] = '1033'
    result = dict(success=False, sourceCopies=0, liveGame=False, steps=[])
    began = time.monotonic()
    try:
        for command in commands:
            run = subprocess.run(command['argv'], cwd=out, env=environment, capture_output=True, timeout=180,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
            (out / (command['name'] + '.log')).write_bytes(run.stdout + run.stderr)
            result['steps'].append(dict(name=command['name'], exit=run.returncode))
            expected = 1 if a.expect_former and command['name'] == 'test' else 0
            if run.returncode != expected: raise RuntimeError(command['name'] + ' failed; see log')
            if expected and b'former sender keeps one reliable group per 128 entries' not in run.stderr:
                raise RuntimeError('Former sender failed for a different reason')
        result['executable'] = identity(executable)
        result['success'] = True
    except Exception as error:
        result['error'] = str(error)
    finally:
        result['inputsUnchanged'] = all(identity(row['path']) == row for row in tracked)
        result['success'] &= result['inputsUnchanged']
        result['seconds'] = time.monotonic() - began
        (out / 'receipt.json').write_text(json.dumps(result, indent=2), 'utf8')
    print(json.dumps(result))
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
