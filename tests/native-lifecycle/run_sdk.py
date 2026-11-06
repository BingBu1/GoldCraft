#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze and test the actual x86 lifecycle wrappers with one compiler job."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
SCENARIOS = [
    'default', 'sequential', 'natural-longjmp', 'supplemental-longjmp',
    'unowned-longjmp', 'clear-observer', 'handler-failure',
    'nested-native', 'nested-native-held', 'nested-frame',
    'capture', 'capture-held', 'foreign-thread', 'foreign-thread-held',
    'foreign-frame', 'foreign-clear', 'wrong-frame', 'frame-bytes',
    'second-hook', 'unhook-failure',
]
UNITS = ['native/client/resource_lifecycle.cpp', 'native/client/resource_native_hud_host.cpp',
         'native/client/resource_native_hud.cpp', 'tests/native/resource_lifecycle_tests.cpp',
         'native/client/resource_model_cache.cpp']
BOUNDARY = ('Actual x86 SDK production lifecycle/Host/native-HUD TUs and actual precache clear/disconnect/reset '
            'wrappers; InlineHook API capture substitutes fixture trampolines. Real CRT setjmp/longjmp in '
            'the original-frame fixture, relocated original PE bytes and original six-byte Surface getter. '
            'No original Host_Frame/HUD_VidInit game execution, plugin integration, deployment, or live gameplay.')


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def verify_mapping(original, mapped, expected):
    raw = original.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == expected, 'original module identity differs'
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    assert raw[:2] == b'MZ' and raw[pe:pe + 4] == b'PE\0\0'
    assert struct.unpack_from('<H', raw, pe + 4)[0] == 0x14c
    opt = pe + 24
    assert struct.unpack_from('<H', raw, opt)[0] == 0x10b
    size, headers = struct.unpack_from('<II', raw, opt + 56)
    rebuilt = bytearray(size)
    rebuilt[:headers] = raw[:headers]
    sections = struct.unpack_from('<H', raw, pe + 6)[0]
    table = opt + struct.unpack_from('<H', raw, pe + 20)[0]
    for i in range(sections):
        _, rva, length, offset = struct.unpack_from('<IIII', raw, table + 40 * i + 8)
        assert offset + length <= len(raw) and rva + length <= size
        rebuilt[rva:rva + length] = raw[offset:offset + length]
    assert bytes(rebuilt) == mapped.read_bytes(), 'mapped fixture differs from exact original PE mapping'
    return dict(original=identity(original), mapped=identity(mapped), mapping='full headers/sections/zero fill match')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='new directory beneath workspace build')
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--include-command', type=Path, required=True)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--mapped-client', type=Path, required=True)
    parser.add_argument('--mapped-engine', type=Path, required=True)
    parser.add_argument('--reuse-receipt', type=Path, help='verified completed-object receipt from an earlier attempt')
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    for path in [args.client, args.engine, args.mapped_client, args.mapped_engine]:
        assert path.resolve(strict=True).is_relative_to(ROOT)
    out.mkdir(parents=True)
    (out / 'temp').mkdir()
    result = dict(success=False, boundary=BOUNDARY, sources=[], inputs=[], steps=[], actualIncludes=[], tests=[])
    try:
        environment = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
        environment['TEMP'] = environment['TMP'] = str(out / 'temp')
        environment['VSLANG'] = '1033'
        result['inputs'] = [identity(path) for path in [__file__, args.compiler, args.environment, args.include_command,
            args.client, args.engine, args.mapped_client, args.mapped_engine]]
        snapshot = out / 'runner-snapshot.py'
        snapshot.write_bytes(Path(__file__).read_bytes())
        result['inputs'].append(identity(snapshot))
        result['mapping'] = [verify_mapping(args.client, args.mapped_client,
                'b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2'),
            verify_mapping(args.engine, args.mapped_engine,
                '9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295')]
        # Only independent copies: test and all quoted local dependency inputs.
        pending, frozen_paths = list(UNITS), set()
        while pending:
            relative = pending.pop()
            original = (ROOT / relative).resolve(strict=True)
            assert original.is_relative_to(ROOT) and not original.is_relative_to(ROOT / 'external')
            relative = original.relative_to(ROOT)
            if relative in frozen_paths:
                continue
            frozen_paths.add(relative)
            target = out / 'source' / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(original.read_bytes())
            result['sources'].append(dict(original=identity(original), frozen=identity(target)))
            for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', original.read_text('utf-8-sig'), re.M):
                candidates = [original.parent / name, ROOT / 'native/include' / name]
                found = next((p.resolve() for p in candidates if p.is_file()), None)
                if found is None:
                    raise RuntimeError('unresolved local include: ' + name)
                pending.append(str(found.relative_to(ROOT)))
        include_command = json.loads(args.include_command.read_text('utf-8-sig'))
        includes = ['/I' + str(out / 'source/native/include')]
        includes += [arg for arg in include_command['arguments'] if arg.startswith(('-I', '/I'))]
        base = [str(args.compiler.resolve()), '--target=i686-pc-windows-msvc', '/nologo', '/std:c++20',
            '/EHsc', '/MT', '/O2', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise',
            '/Gy', '/Gw', '/utf-8', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', '/DNO_MALLOC_OVERRIDE',
            '/showIncludes', *includes]
        included = {}
        reused = {}
        if args.reuse_receipt:
            receipt = json.loads(args.reuse_receipt.read_text('utf-8-sig'))
            assert identity(receipt['result']['path']) == receipt['result']
            assert identity(receipt['runnerSnapshot']['path']) == receipt['runnerSnapshot']
            previous = json.loads(Path(receipt['result']['path']).read_text('utf-8-sig'))
            for source in previous['sources']:
                assert identity(source['original']['path']) == source['original']
                assert identity(source['frozen']['path']) == source['frozen']
            for item in previous['inputs']:
                if Path(item['path']).resolve() != Path(__file__).resolve():
                    assert identity(item['path']) == item
                else:
                    assert item['sha256'] == receipt['runnerSnapshot']['sha256']
            for item in previous['actualIncludes']:
                assert identity(item['path']) == item
                included[item['path']] = item
            for item in receipt['objects']:
                assert identity(item['object']['path']) == item['object']
                assert identity(item['compileLog']['path']) == item['compileLog']
                reused[item['unit']] = item
            result['reuse'] = dict(receipt=identity(args.reuse_receipt), previousResult=receipt['result'],
                                   objects=list(reused.values()))
            result['inputs'] += [identity(args.reuse_receipt), receipt['result'], receipt['runnerSnapshot']]
            result['inputs'] += [item['object'] for item in reused.values()]

        def execute(name, command, timeout=120, require_success=True):
            start = time.monotonic()
            completed = subprocess.run(command, cwd=out, env=environment, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, timeout=timeout)
            log = out / (name + '.log')
            log.write_bytes(completed.stdout)
            output = completed.stdout.decode('utf-8', 'replace')
            actual = [identity(p.strip()) for p in sorted(set(re.findall(r'Note: including file:\s*(.+)', output)))]
            row = dict(name=name, arguments=command, exit=completed.returncode,
                       seconds=time.monotonic() - start, log=identity(log), includes=actual)
            result['steps'].append(row)
            save(out / 'result.json', result)
            print(name, completed.returncode, flush=True)
            if require_success and completed.returncode:
                print(output[-7000:], flush=True)
                raise RuntimeError(name + ' failed')
            return row, output

        # Discover actual includes before code generation; later compilations must
        # use exactly this dependency set, with hashes verified before and after.
        for unit in UNITS:
            if Path(unit).stem in reused:
                continue
            row, _ = execute('dependencies-' + Path(unit).stem,
                             [*base, '/Zs', str(out / 'source' / unit)])
            for item in row['includes']:
                included[item['path']] = item
        result['actualIncludes'] = list(included.values())
        all_inputs = result['inputs'] + result['actualIncludes'] + [row[key] for row in result['sources']
                       for key in ['original', 'frozen']]

        def verify_inputs():
            changed = [item['path'] for item in all_inputs if identity(item['path']) != item]
            result['changed'] = changed
            assert not changed, 'input drift: ' + repr(changed)

        verify_inputs()
        save(out / 'plan.json', result)
        objects = []
        for unit in UNITS:
            verify_inputs()
            if Path(unit).stem in reused:
                objects.append(Path(reused[Path(unit).stem]['object']['path']))
                continue
            obj = out / (Path(unit).stem + '.obj')
            row, _ = execute('compile-' + Path(unit).stem,
                             [*base, '/c', '/Fo' + str(obj), str(out / 'source' / unit)])
            assert all(item['path'] in included and item == included[item['path']] for item in row['includes'])
            objects.append(obj)
            result['compiledOutputs'] = [identity(path) for path in objects]
            save(out / 'result.json', result)
        verify_inputs()
        executable = out / 'resource_lifecycle_tests.exe'
        execute('link', [str(args.compiler.resolve()), '--target=i686-pc-windows-msvc', '-fuse-ld=lld',
            '/MT', *map(str, objects), '/Fe' + str(executable), '/link', '/OPT:REF', '/threads:1', 'user32.lib', 'opengl32.lib'])
        for scenario in SCENARIOS:
            row, output = execute('test-' + scenario, [str(executable), str(args.mapped_client.resolve()),
                str(args.mapped_engine.resolve()), scenario], 45, False)
            parsed = re.search(r'resource lifecycle .+: (\d+) checks, (\d+) failures', output)
            result['tests'].append(dict(scenario=scenario, exit=row['exit'],
                checks=int(parsed[1]) if parsed else None, failures=int(parsed[2]) if parsed else None))
            print(output.strip(), flush=True)
        verify_inputs()
        result['outputs'] = [identity(path) for path in [*objects, executable]]
        result['checks'] = sum(test['checks'] or 0 for test in result['tests'])
        result['success'] = all(test['exit'] == 0 and test['failures'] == 0 for test in result['tests'])
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        save(out / 'result.json', result)
        print(json.dumps(dict(success=result['success'], result=identity(out / 'result.json'))), flush=True)
    return 0 if result['success'] else 1


if __name__ == '__main__':
    sys.exit(main())
