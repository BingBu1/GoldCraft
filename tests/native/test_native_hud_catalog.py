#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify original HUD catalog bytes, generate catalogs, and run the real x86 resolver."""
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def pe_reader(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[:2] == b'MZ' and data[pe:pe + 4] == b'PE\0\0'
    assert struct.unpack_from('<H', data, pe + 4)[0] == 0x14c
    assert struct.unpack_from('<H', data, pe + 24)[0] == 0x10b
    assert struct.unpack_from('<I', data, pe + 52)[0] == 0x10000000
    count, optional = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    sections = [struct.unpack_from('<IIII', data, pe + 24 + optional + 40 * i + 8) for i in range(count)]

    def read(rva, size):
        for _, base, length, offset in sections:
            if base <= rva and rva + size <= base + length:
                return data[offset + rva - base:offset + rva - base + size]
        raise ValueError(f'Unbacked RVA {rva:#x}')
    return read


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--include-command', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    assert all(p.resolve(strict=True).is_relative_to(ROOT) for p in [args.client, args.engine, args.evidence])
    out.mkdir(parents=True)
    (out / 'temp').mkdir()
    environment = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    environment['TEMP'] = environment['TMP'] = str(out / 'temp')
    environment['VSLANG'] = '1033'
    inputs = [identity(p) for p in [__file__, args.client, args.engine, args.compiler,
                                    args.environment, args.include_command]]
    sources = ['tools/Build-ClientGameData.py', 'tools/Build-PrecacheGameData.py', 'tools/Inspect-EngineIdentity.py',
               'tests/native/native_hud_catalog_tests.cpp',
               'external/MetaHookSv-20261007/MetaHook/src/GameData.cpp',
               'external/MetaHookSv-20261007/MetaHook/src/GameData.h']
    sources += ['native/client/' + name for name in ['native_hud_client_10210.json', 'native_hud_engine_10210.json',
                'vitals_10210.json', 'motion_10210.json', 'spectator_menu_10210.json', 'hud_clock_10210.json',
                'shared_random_10210.json', 'precache_10210.json', 'userinfo_10210.json', 'resource_sync_10210.json']]
    sources += ['native/csol/sequence_engine_10210.json']
    frozen = []
    for relative in sources:
        source, target = ROOT / relative, out / 'source' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())
        frozen.append(dict(original=identity(source), frozen=identity(target)))
    # Compare only the ten new catalog windows with saved IDA and exact PE.
    captures = {
        'client': ['client-hud-original-bytes.json', 'client-ctor-dtor-original-bytes.json',
                   'client-hud-final-original-bytes.json', 'client-allocator-original-bytes.json',
                   'client-native-surface-getter-original-bytes.json', 'client-native-surface-provider-original-bytes.json'],
        'engine': ['hw-directory-allocator-original-bytes.json'],
    }
    spans = []
    for role, binary in [('client', args.client), ('engine', args.engine)]:
        metadata = json.loads((ROOT / f'native/client/native_hud_{role}_10210.json').read_text())
        actual = identity(binary)
        assert actual['sha256'] == metadata['sha256']
        raw = pe_reader(binary.read_bytes())
        regions = []
        for name in captures[role]:
            path = args.evidence / name
            inputs.append(identity(path))
            capture = json.loads(path.read_text('utf-8-sig'))
            assert capture['sha256'] == actual['sha256']
            regions += capture['response']['result']['structuredContent']['result']
        for entry in metadata['verification']:
            rva, expected = int(entry['rva'], 16), bytes.fromhex(entry['bytes'])
            assert raw(rva, len(expected)) == expected
            va = 0x10000000 + rva
            matched = False
            for region in regions:
                start = int(region['addr'], 16)
                data = bytes(int(token, 16) for token in region['data'].split())
                if start <= va and va + len(expected) <= start + len(data):
                    assert data[va - start:va - start + len(expected)] == expected
                    matched = True
                    break
            assert matched, f'No original IDA window for {role} {rva:#x}'
            spans.append(dict(module=role, rva=hex(rva), bytes=len(expected), sha256=hashlib.sha256(expected).hexdigest()))
    steps = []

    def execute(name, command, expected_exit=0):
        start = time.monotonic()
        result = subprocess.run(command, cwd=ROOT, env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180)
        log = out / (name + '.log')
        log.write_bytes(result.stdout)
        included = sorted(set(re.findall(r'Note: including file:\s*(.+)', result.stdout.decode('utf-8', 'replace'))))
        row = dict(name=name, command=command, exit=result.returncode, expected=expected_exit,
                   seconds=time.monotonic() - start, log=identity(log), includes=[identity(p.strip()) for p in included])
        steps.append(row)
        save(out / 'steps.json', steps)
        print(name, result.returncode, flush=True)
        if result.returncode != expected_exit:
            print(result.stdout.decode('utf-8', 'replace')[-6000:])
            raise RuntimeError(name + ' failed')
        return result.stdout.decode('utf-8', 'replace')

    catalogs, bad_files = [], []
    for role, binary, script, flag in [('client', args.client, 'Build-ClientGameData.py', '--client'),
                                      ('engine', args.engine, 'Build-PrecacheGameData.py', '--engine')]:
        destination = out / ('catalog-' + role)
        execute('generate-' + role, [sys.executable, str(ROOT / 'tools' / script), flag, str(binary.resolve()),
                                    '--output', str(destination)])
        catalogs.append(destination)
        # Preserve all reviewed instructions but change file identity in a new file.
        changed = out / ('foreign-' + role + '.dll')
        changed.write_bytes(binary.read_bytes() + b'\0')
        bad_files.append(changed)
        rejected = out / ('rejected-' + role)
        text = execute('reject-' + role, [sys.executable, str(ROOT / 'tools' / script), flag, str(changed),
                                         '--output', str(rejected)], 1)
        assert 'Unanalyzed' in text and 'identity' in text and not rejected.exists()
    command = json.loads(args.include_command.read_text('utf-8-sig'))
    includes = [arg for arg in command['arguments'] if arg.startswith(('-I', '/I'))]
    includes += ['/I' + str(ROOT / 'external/MetaHookSv-20261007/MetaHook/thirdparty/Chocobo1Hash/src')]
    obj, executable = out / 'catalog.obj', out / 'catalog.exe'
    execute('compile', [str(args.compiler.resolve()), '--target=i686-pc-windows-msvc', '/nologo', '/std:c++20',
        '/EHsc', '/MT', '/O2', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise',
        '/utf-8', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', '/showIncludes', *includes,
        '/c', '/Fo' + str(obj), str(out / 'source/tests/native/native_hud_catalog_tests.cpp')])
    execute('link', [str(args.compiler.resolve()), '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT',
                    str(obj), '/Fe' + str(executable), '/link', '/OPT:REF', '/threads:1', 'user32.lib'])
    execute('test', [str(executable), *map(str, catalogs), str(args.client.resolve()), str(args.engine.resolve()),
                     *map(str, bad_files)])
    included = {entry['path']: entry for step in steps for entry in step['includes']}
    all_inputs = inputs + [row[key] for row in frozen for key in ['original', 'frozen']] + list(included.values())
    changed = [entry['path'] for entry in all_inputs if identity(entry['path']) != entry]
    outputs = [identity(path) for catalog in catalogs for path in catalog.iterdir()] + [identity(obj), identity(executable)]
    save(out / 'result.json', dict(success=not changed, sources=frozen, inputs=inputs, originalByteWindows=spans,
        steps=steps, actualIncludes=list(included.values()), outputs=outputs, changed=changed,
        boundary='Exact original PE and saved IDA windows; real GameData parser/hash/ResolveGameSymbol; no game DLL execution, deployment, or lifecycle proof.'))
    assert not changed
    print(json.dumps(dict(success=True, windows=len(spans), bytes=sum(x['bytes'] for x in spans),
                          actualIncludes=len(included), result=identity(out / 'result.json'))))


if __name__ == '__main__':
    main()
